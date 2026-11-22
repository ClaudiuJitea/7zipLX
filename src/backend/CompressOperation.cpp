#include "CompressOperation.h"
#include "ProgressState.h"
#include "PromptDispatcher.h"
#include "Unicode.h"
#include "SevenZipEngine.h"

#include <QFileInfo>
#include <QDir>

#include "Common/MyCom.h"
#include "Common/MyException.h"
#include "Windows/ErrorMsg.h"
#include "Windows/PropVariant.h"
#include "7zip/UI/Common/ArchiveOpenCallback.h"
#include "7zip/UI/Common/Update.h"
#include "7zip/UI/Common/UpdateCallback.h"
#include "7zip/UI/Common/DirItem.h"
#include "7zip/UI/Common/Property.h"
#include "7zip/UI/Agent/Agent.h"
#include "7zip/Common/FileStreams.h"

#include <cstdio>

namespace {

QString hrText(HRESULT hr)
{
  return QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

class OpenBridge final : public IOpenCallbackUI
{
public:
  OpenBridge(ProgressState *progress, PromptDispatcher *prompts, const QString &password)
      : m_progress(progress), m_prompts(prompts)
  {
    if (!password.isEmpty())
    {
      m_password = q2u(password);
      m_passwordDefined = true;
    }
  }
  HRESULT Open_CheckBreak() override { return (m_progress && m_progress->isCanceled()) ? E_ABORT : S_OK; }
  HRESULT Open_SetTotal(const UInt64 *, const UInt64 *bytes) override
  {
    if (m_progress) m_progress->setTotals(bytes ? *bytes : 0);
    return Open_CheckBreak();
  }
  HRESULT Open_SetCompleted(const UInt64 *, const UInt64 *bytes) override
  {
    if (m_progress && bytes) m_progress->setCompleted(*bytes);
    return Open_CheckBreak();
  }
  HRESULT Open_Finished() override { return S_OK; }
  HRESULT Open_CryptoGetTextPassword(BSTR *password) override
  {
    if (!m_passwordDefined)
    {
      QString pw;
      bool ok = false;
      if (!m_prompts || !m_prompts->askPassword(&pw, &ok, QStringLiteral("Password required")))
        return E_ABORT;
      m_password = q2u(pw);
      m_passwordDefined = true;
    }
    return StringToBstr(m_password, password);
  }
private:
  ProgressState *m_progress = nullptr;
  PromptDispatcher *m_prompts = nullptr;
  UString m_password;
  bool m_passwordDefined = false;
};

class UpdateBridge final : public IUpdateCallbackUI2
{
public:
  UpdateBridge(ProgressState *progress, PromptDispatcher *prompts, const QString &password)
      : m_progress(progress), m_prompts(prompts)
  {
    if (!password.isEmpty())
    {
      m_password = q2u(password);
      m_passwordDefined = true;
    }
  }

  QStringList messages() const { return m_messages; }
  quint64 files() const { return m_files; }

  HRESULT OpenResult(const CCodecs *, const CArchiveLink &, const wchar_t *name, HRESULT result) override
  {
    if (result != S_OK && result != S_FALSE)
      add(QStringLiteral("Archive: %1 (%2)").arg(u2q(name), hrText(result)));
    return S_OK;
  }
  HRESULT StartScanning() override { if (m_progress) m_progress->setStatus(QStringLiteral("Scanning...")); return check(); }
  HRESULT FinishScanning(const CDirItemsStat &st) override
  {
    if (m_progress) m_progress->setTotals(st.GetTotalBytes(), st.NumFiles + st.NumAltStreams);
    return check();
  }
  HRESULT StartOpenArchive(const wchar_t *name) override
  {
    if (m_progress) m_progress->setStatus(QStringLiteral("Opening %1").arg(u2q(name)));
    return check();
  }
  HRESULT StartArchive(const wchar_t *name, bool /*updating*/) override
  {
    if (m_progress)
    {
      m_progress->setTitle(QStringLiteral("Compressing"));
      m_progress->setStatus(QStringLiteral("Creating %1").arg(u2q(name)));
    }
    return check();
  }
  HRESULT FinishArchive(const CFinishArchiveStat &) override
  {
    if (m_progress) m_progress->setStatus(QStringLiteral("Archive finished"));
    return S_OK;
  }
  HRESULT DeletingAfterArchiving(const FString &path, bool /*isDir*/) override
  {
    if (m_progress) m_progress->setCurrentFile(QStringLiteral("Deleting %1").arg(fs2q(path)));
    return check();
  }
  HRESULT FinishDeletingAfterArchiving() override { return S_OK; }
  HRESULT MoveArc_Start(const wchar_t *src, const wchar_t *, UInt64, Int32) override
  {
    if (m_progress) m_progress->setCurrentFile(QStringLiteral("Moving %1").arg(u2q(src)));
    return check();
  }
  HRESULT MoveArc_Progress(UInt64 total, UInt64 current) override
  {
    if (m_progress) m_progress->setCompleted(current, 0);
    return total ? check() : S_OK;
  }
  HRESULT MoveArc_Finish() override { return S_OK; }

  HRESULT WriteSfx(const wchar_t *, UInt64) override { return S_OK; }
  HRESULT SetTotal(UInt64 size) override { if (m_progress) m_progress->setTotals(size); return check(); }
  HRESULT SetCompleted(const UInt64 *completeValue) override
  {
    if (m_progress && completeValue) m_progress->setCompleted(*completeValue);
    return check();
  }
  HRESULT SetRatioInfo(const UInt64 *inSize, const UInt64 *outSize) override
  {
    if (m_progress) m_progress->setRatio(inSize ? *inSize : 0, outSize ? *outSize : 0);
    return check();
  }
  HRESULT CheckBreak() override { return check(); }
  HRESULT SetNumItems(const CArcToDoStat &stat) override
  {
    if (m_progress) m_progress->setTotals(0, stat.Get_NumDataItems_Total());
    return check();
  }
  HRESULT GetStream(const wchar_t *name, bool /*isDir*/, bool /*isAnti*/, UInt32 /*mode*/) override
  {
    if (m_progress) m_progress->setCurrentFile(u2q(name));
    return check();
  }
  HRESULT OpenFileError(const FString &path, DWORD systemError) override
  {
    add(QStringLiteral("Cannot open %1 (%2)").arg(fs2q(path), hrText(HRESULT_FROM_WIN32(systemError))));
    return S_OK;
  }
  HRESULT ReadingFileError(const FString &path, DWORD systemError) override
  {
    add(QStringLiteral("Cannot read %1 (%2)").arg(fs2q(path), hrText(HRESULT_FROM_WIN32(systemError))));
    return S_OK;
  }
  HRESULT SetOperationResult(Int32 /*opRes*/) override
  {
    ++m_files;
    if (m_progress) m_progress->setCompleted(0, m_files);
    return check();
  }
  HRESULT ReportExtractResult(Int32 opRes, Int32, const wchar_t *name) override
  {
    if (opRes != NArchive::NExtract::NOperationResult::kOK)
      add(QStringLiteral("Error: %1").arg(u2q(name)));
    return S_OK;
  }
  HRESULT ReportUpdateOperation(UInt32 /*op*/, const wchar_t *name, bool /*isDir*/) override
  {
    if (m_progress) m_progress->setCurrentFile(u2q(name));
    return check();
  }
  HRESULT CryptoGetTextPassword2(Int32 *passwordIsDefined, BSTR *password) override
  {
    *password = nullptr;
    if (passwordIsDefined)
      *passwordIsDefined = m_passwordDefined ? 1 : 0;
    if (!m_passwordDefined)
      return S_OK;
    return StringToBstr(m_password, password);
  }
  HRESULT CryptoGetTextPassword(BSTR *password) override
  {
    if (!m_passwordDefined)
    {
      QString pw;
      bool ok = false;
      if (!m_prompts || !m_prompts->askPassword(&pw, &ok, QStringLiteral("Password required")))
        return E_ABORT;
      m_password = q2u(pw);
      m_passwordDefined = true;
    }
    return StringToBstr(m_password, password);
  }
  HRESULT ShowDeleteFile(const wchar_t *, bool) override { return S_OK; }

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
  UString m_password;
  bool m_passwordDefined = false;
  QStringList m_messages;
  quint64 m_files = 0;
};

void addProperty(CObjectVector<CProperty> &props, const char *name, const UString &value)
{
  CProperty p;
  p.Name = name;
  p.Value = value;
  props.Add(p);
}

void addUIntProperty(CObjectVector<CProperty> &props, const char *name, UInt32 value)
{
  UString s;
  s.Add_UInt32(value);
  addProperty(props, name, s);
}

void addSizeProperty(CObjectVector<CProperty> &props, const char *name, UInt64 value)
{
  UString s;
  s.Add_UInt64(value);
  s.Add_Char('b');
  addProperty(props, name, s);
}

} // namespace

OperationResult compressFiles(const QStringList &files,
                              const CompressOptions &options,
                              ProgressState *progress,
                              PromptDispatcher *prompts)
{
  OperationResult result;
  if (files.isEmpty())
  {
    result.error = QStringLiteral("No files selected");
    return result;
  }
  QString engineError;
  if (!SevenZipEngine::instance().initialize(&engineError))
  {
    result.error = engineError;
    return result;
  }
  if (!g_CodecsObj)
  {
    result.error = QStringLiteral("7-Zip codecs are not initialized");
    return result;
  }

  const QFileInfo archiveInfo(options.archivePath);
  if (archiveInfo.absolutePath().isEmpty())
  {
    result.error = QStringLiteral("Invalid archive path");
    return result;
  }
  QDir().mkpath(archiveInfo.absolutePath());

  NWildcard::CCensor censor;
  for (const QString &path : files)
  {
    const QString abs = QFileInfo(path).absoluteFilePath();
    censor.AddPreItem_NoWildcard(q2u(abs));
  }

  CUpdateOptions updateOptions;
  updateOptions.SetActionCommand_Add();
  updateOptions.PathMode = NWildcard::k_RelatPath;
  updateOptions.MethodMode.Type_Defined = false;
  if (options.formatIndex >= 0)
  {
    COpenType type;
    type.FormatIndex = options.formatIndex;
    updateOptions.MethodMode.Type = type;
    updateOptions.MethodMode.Type_Defined = true;
  }

  auto &props = updateOptions.MethodMode.Properties;
  addUIntProperty(props, "x", UInt32(qBound(0, options.level, 9)));
  if (!options.method.isEmpty())
    addProperty(props, options.formatIndex == SevenZipEngine::instance().formatIndex(QStringLiteral("7z")) ? "0" : "m",
                 q2u(options.method));
  if (options.dictionary != 0)
    addSizeProperty(props, options.formatIndex == SevenZipEngine::instance().formatIndex(QStringLiteral("7z")) ? "0d" : "d",
                    options.dictionary);
  if (options.solid)
    addProperty(props, "s", UString("on"));
  if (options.numThreads > 0)
    addUIntProperty(props, "mt", UInt32(options.numThreads));
  if (options.encryptHeaders)
    addProperty(props, "he", UString("on"));
  if (options.volumeSize != 0)
    updateOptions.VolumesSizes.Add(options.volumeSize);

  CObjectVector<COpenType> types;
  if (options.formatIndex >= 0)
  {
    COpenType type;
    type.FormatIndex = options.formatIndex;
    types.Add(type);
  }

  if (progress)
  {
    progress->setTitle(QStringLiteral("Compressing"));
    progress->setStatus(QStringLiteral("Preparing..."));
  }

  OpenBridge openBridge(progress, prompts, options.password);
  UpdateBridge updateBridge(progress, prompts, options.password);
  CUpdateErrorInfo errorInfo;
  HRESULT res = E_FAIL;
  try
  {
    res = UpdateArchive(g_CodecsObj, types, q2u(archiveInfo.absoluteFilePath()), censor,
                        updateOptions, errorInfo, &openBridge, &updateBridge, true);
  }
  catch (const CSystemException &e)
  {
    res = e.ErrorCode;
  }
  catch (const UString &s)
  {
    result.error = u2q(s);
  }
  catch (const std::exception &e)
  {
    result.error = QString::fromUtf8(e.what());
  }
  catch (...)
  {
    result.error = QStringLiteral("Unknown error while compressing");
  }

  result.messages = updateBridge.messages();
  result.files = updateBridge.files();
  if (res == E_ABORT)
  {
    result.canceled = true;
    result.error = QStringLiteral("Operation canceled");
  }
  else if (res != S_OK)
  {
    if (result.error.isEmpty())
    {
      if (!errorInfo.Message.IsEmpty())
        result.error = QString::fromUtf8(errorInfo.Message.Ptr());
      else
        result.error = QStringLiteral("Compression failed (%1)").arg(hrText(res));
    }
  }
  else
  {
    result.ok = true;
  }

  if (progress)
  {
    progress->setStatus(result.ok ? QStringLiteral("Finished")
                                  : (result.canceled ? QStringLiteral("Canceled") : QStringLiteral("Failed")));
    for (const QString &m : result.messages)
      progress->addMessage(m);
  }
  return result;
}
