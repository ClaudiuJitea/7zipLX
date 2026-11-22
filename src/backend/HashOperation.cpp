#include "HashOperation.h"
#include "ProgressState.h"
#include "PromptDispatcher.h"
#include "Unicode.h"
#include "SevenZipEngine.h"

#include <QFileInfo>

#include "Common/MyCom.h"
#include "Common/MyWindows.h"
#include "Windows/ErrorMsg.h"
#include "7zip/UI/Common/HashCalc.h"
#include "7zip/UI/Common/DirItem.h"

#include <cstdio>

namespace {

QString hrText(HRESULT hr)
{
  return QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

class HashCallback final : public IHashCallbackUI
{
public:
  HashCallback(ProgressState *progress, PromptDispatcher *prompts)
      : m_progress(progress), m_prompts(prompts) {}

  QStringList lines() const { return m_lines; }
  QStringList messages() const { return m_messages; }

  HRESULT StartScanning() override
  {
    if (m_progress) m_progress->setStatus(QStringLiteral("Scanning..."));
    return check();
  }
  HRESULT FinishScanning(const CDirItemsStat &st) override
  {
    if (m_progress) m_progress->setTotals(st.GetTotalBytes(), st.NumFiles + st.NumAltStreams);
    return check();
  }
  HRESULT SetNumFiles(UInt64 numFiles) override
  {
    if (m_progress) m_progress->setTotals(0, numFiles);
    return check();
  }
  HRESULT SetTotal(UInt64 size) override
  {
    if (m_progress) m_progress->setTotals(size);
    return check();
  }
  HRESULT SetCompleted(const UInt64 *completeValue) override
  {
    if (m_progress && completeValue) m_progress->setCompleted(*completeValue);
    return check();
  }
  HRESULT CheckBreak() override { return check(); }
  HRESULT BeforeFirstFile(const CHashBundle &) override
  {
    if (m_progress) m_progress->setStatus(QStringLiteral("Calculating..."));
    return check();
  }
  HRESULT GetStream(const wchar_t *name, bool /*isFolder*/) override
  {
    if (m_progress) m_progress->setCurrentFile(u2q(name));
    return check();
  }
  HRESULT OpenFileError(const FString &path, DWORD systemError) override
  {
    add(QStringLiteral("Cannot read %1 (%2)").arg(fs2q(path), hrText(HRESULT_FROM_WIN32(systemError))));
    return S_OK;
  }
  HRESULT SetOperationResult(UInt64 fileSize, const CHashBundle &hb, bool /*showHash*/) override
  {
    ++m_files;
    if (m_progress) m_progress->setCompleted(0, m_files);
    if (fileSize == 0 && hb.NumErrors != 0)
      return check();
    return check();
  }
  HRESULT AfterLastFile(CHashBundle &hb) override
  {
    for (unsigned i = 0; i < hb.Hashers.Size(); ++i)
    {
      const CHasherState &h = hb.Hashers[i];
      char buf[k_HashCalc_DigestSize_Max * 2 + 1];
      h.WriteToString(k_HashCalc_Index_DataSum, buf);
      m_lines << QStringLiteral("%1: %2").arg(QString::fromUtf8(h.Name.Ptr()), QString::fromLatin1(buf));
      if (hb.NumFiles + hb.NumDirs > 1)
      {
        h.WriteToString(k_HashCalc_Index_NamesSum, buf);
        m_lines << QStringLiteral("%1(names): %2").arg(QString::fromUtf8(h.Name.Ptr()), QString::fromLatin1(buf));
      }
    }
    if (hb.NumErrors != 0)
      m_lines << QStringLiteral("Errors: %1").arg(hb.NumErrors);
    return S_OK;
  }
  HRESULT ScanError(const FString &path, DWORD systemError) override
  {
    add(QStringLiteral("Scan error: %1 (%2)").arg(fs2q(path), hrText(HRESULT_FROM_WIN32(systemError))));
    return S_OK;
  }
  HRESULT ScanProgress(const CDirItemsStat &st, const FString &path, bool /*isDir*/) override
  {
    if (m_progress)
    {
      m_progress->setTotals(st.GetTotalBytes(), st.NumFiles + st.NumAltStreams);
      m_progress->setCurrentFile(fs2q(path));
    }
    return check();
  }

private:
  HRESULT check() const { return (m_progress && m_progress->isCanceled()) ? E_ABORT : S_OK; }
  void add(const QString &s)
  {
    m_messages.append(s);
    if (m_progress) m_progress->addMessage(s);
  }

  ProgressState *m_progress = nullptr;
  PromptDispatcher *m_prompts = nullptr;
  QStringList m_lines;
  QStringList m_messages;
  quint64 m_files = 0;
};

} // namespace

HashResult hashFiles(const QStringList &files,
                     const HashOptions &options,
                     ProgressState *progress,
                     PromptDispatcher *prompts)
{
  HashResult result;
  if (files.isEmpty())
  {
    result.operation.error = QStringLiteral("No files selected");
    return result;
  }
  QString engineError;
  if (!SevenZipEngine::instance().initialize(&engineError))
  {
    result.operation.error = engineError;
    return result;
  }

  NWildcard::CCensor censor;
  for (const QString &path : files)
    censor.AddPreItem_NoWildcard(q2u(QFileInfo(path).absoluteFilePath()));

  CHashOptions hashOptions;
  hashOptions.PathMode = NWildcard::k_RelatPath;
  hashOptions.SymLinks.Val = true;
  censor.AddPathsToCensor(hashOptions.PathMode);
  if (options.methods.isEmpty())
    hashOptions.Methods.Add(UString("SHA256"));
  else
    for (const QString &m : options.methods)
      hashOptions.Methods.Add(q2u(m));

  if (progress)
  {
    progress->setTitle(QStringLiteral("Hashing"));
    progress->setStatus(QStringLiteral("Preparing..."));
  }

  HashCallback callback(progress, prompts);
  AString errorInfo;
  HRESULT res = E_FAIL;
  try
  {
#ifdef Z7_EXTERNAL_CODECS
    res = HashCalc(SevenZipEngine::instance().externalCodecs(), censor, hashOptions, errorInfo, &callback);
#else
    res = HashCalc(censor, hashOptions, errorInfo, &callback);
#endif
  }
  catch (const UString &s)
  {
    result.operation.error = u2q(s);
  }
  catch (const std::exception &e)
  {
    result.operation.error = QString::fromUtf8(e.what());
  }
  catch (...)
  {
    result.operation.error = QStringLiteral("Unknown error while hashing");
  }

  result.lines = callback.lines();
  result.operation.details = result.lines;
  result.operation.messages = callback.messages();
  if (res == E_ABORT)
  {
    result.operation.canceled = true;
    result.operation.error = QStringLiteral("Operation canceled");
  }
  else if (res != S_OK)
  {
    if (result.operation.error.isEmpty())
      result.operation.error = errorInfo.IsEmpty()
          ? QStringLiteral("Hashing failed (%1)").arg(hrText(res))
          : QString::fromUtf8(errorInfo.Ptr());
  }
  else
  {
    result.operation.ok = true;
  }

  if (progress)
  {
    progress->setStatus(result.operation.ok ? QStringLiteral("Finished")
                                            : (result.operation.canceled ? QStringLiteral("Canceled") : QStringLiteral("Failed")));
    for (const QString &m : result.operation.messages)
      progress->addMessage(m);
  }
  return result;
}
