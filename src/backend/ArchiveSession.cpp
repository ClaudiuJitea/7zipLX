#include "ArchiveSession.h"
#include "ProgressState.h"
#include "PromptDispatcher.h"
#include "Unicode.h"
#include "PropVariantQt.h"
#include "SevenZipEngine.h"

#include <QDir>
#include <QFileInfo>

#include "Common/MyCom.h"
#include "Common/ComTry.h"
#include "Windows/ErrorMsg.h"
#include "Windows/FileDir.h"
#include "Windows/FileFind.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"

#include "7zip/PropID.h"
#include "7zip/Common/FileStreams.h"
#include "7zip/UI/Common/ArchiveOpenCallback.h"
#include "7zip/UI/Common/IFileExtractCallback.h"
#include "7zip/UI/Agent/Agent.h"
#include "7zip/UI/Agent/IFolderArchive.h"
#include "7zip/UI/FileManager/IFolder.h"

#include <cwchar>

using namespace NWindows;

namespace {

QString hrText(HRESULT hr)
{
  return QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

QDateTime ftToQ(const FILETIME *ft)
{
  return ft ? fileTimeToDateTime(*ft) : QDateTime();
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

  HRESULT Open_CheckBreak() override
  {
    return m_progress && m_progress->isCanceled() ? E_ABORT : S_OK;
  }

  HRESULT Open_SetTotal(const UInt64 *files, const UInt64 *bytes) override
  {
    if (m_progress)
    {
      m_progress->setTotals(bytes ? *bytes : 0, files ? *files : 0);
      m_progress->setStatus(QStringLiteral("Opening archive..."));
    }
    return Open_CheckBreak();
  }

  HRESULT Open_SetCompleted(const UInt64 *files, const UInt64 *bytes) override
  {
    if (m_progress)
      m_progress->setCompleted(bytes ? *bytes : 0, files ? *files : 0);
    return Open_CheckBreak();
  }

  HRESULT Open_Finished() override
  {
    if (m_progress)
      m_progress->setStatus(QStringLiteral("Archive opened"));
    return S_OK;
  }

  HRESULT Open_CryptoGetTextPassword(BSTR *password) override
  {
    if (!m_passwordDefined)
    {
      if (!m_prompts)
        return E_ABORT;
      QString pw;
      bool ok = false;
      m_prompts->askPassword(&pw, &ok, QStringLiteral("Password required"));
      if (!ok)
        return E_ABORT;
      m_password = q2u(pw);
      m_passwordDefined = true;
    }
    return StringToBstr(m_password, password);
  }

  bool passwordDefined() const { return m_passwordDefined; }
  const UString &password() const { return m_password; }

private:
  ProgressState *m_progress = nullptr;
  PromptDispatcher *m_prompts = nullptr;
  UString m_password;
  bool m_passwordDefined = false;
};

class ExtractBridge final
    : public IFolderArchiveExtractCallback,
      public IFolderArchiveExtractCallback2,
      public ICryptoGetTextPassword,
      public CMyUnknownImp
{
public:
  Z7_COM_QI_BEGIN2(IFolderArchiveExtractCallback)
    Z7_COM_QI_ENTRY(IFolderArchiveExtractCallback2)
    Z7_COM_QI_ENTRY(ICryptoGetTextPassword)
  Z7_COM_QI_END
  Z7_COM_ADDREF_RELEASE

  Z7_IFACE_COM7_IMP(IProgress)
  Z7_IFACE_COM7_IMP(IFolderArchiveExtractCallback)
  Z7_IFACE_COM7_IMP(IFolderArchiveExtractCallback2)
  Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)

public:
  ExtractBridge(ProgressState *progress, PromptDispatcher *prompts, const QString &password)
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

private:
  ProgressState *m_progress = nullptr;
  PromptDispatcher *m_prompts = nullptr;
  UString m_password;
  bool m_passwordDefined = false;
  OverwriteAnswer m_overwriteAll = OverwriteAnswer::Cancel;
  QStringList m_messages;
  quint64 m_files = 0;

  HRESULT checkBreak() const
  {
    return (m_progress && m_progress->isCanceled()) ? E_ABORT : S_OK;
  }
};

Z7_COM7F_IMF(ExtractBridge::SetTotal(UInt64 size))
{
  if (m_progress)
    m_progress->setTotals(size);
  return checkBreak();
}

Z7_COM7F_IMF(ExtractBridge::SetCompleted(const UInt64 *completeValue))
{
  if (m_progress && completeValue)
    m_progress->setCompleted(*completeValue);
  return checkBreak();
}

Z7_COM7F_IMF(ExtractBridge::AskOverwrite(
    const wchar_t *existName, const FILETIME *existTime, const UInt64 *existSize,
    const wchar_t *newName, const FILETIME *newTime, const UInt64 *newSize,
    Int32 *answer))
{
  if (!answer)
    return E_INVALIDARG;

  if (m_overwriteAll == OverwriteAnswer::YesToAll)
  {
    *answer = NOverwriteAnswer::kYes;
    return S_OK;
  }
  if (m_overwriteAll == OverwriteAnswer::NoToAll)
  {
    *answer = NOverwriteAnswer::kNo;
    return S_OK;
  }

  OverwriteInfo info;
  info.existingName = u2q(existName);
  info.newName = u2q(newName);
  info.existingTime = ftToQ(existTime);
  info.newTime = ftToQ(newTime);
  info.existingSize = existSize ? quint64(*existSize) : 0;
  info.newSize = newSize ? quint64(*newSize) : 0;
  info.existingSizeKnown = existSize != nullptr;
  info.newSizeKnown = newSize != nullptr;

  if (!m_prompts)
  {
    *answer = NOverwriteAnswer::kCancel;
    return E_ABORT;
  }

  const OverwriteAnswer a = m_prompts->askOverwrite(info);
  if (a == OverwriteAnswer::YesToAll)
    m_overwriteAll = a;
  else if (a == OverwriteAnswer::NoToAll)
    m_overwriteAll = a;

  switch (a)
  {
    case OverwriteAnswer::Yes: *answer = NOverwriteAnswer::kYes; break;
    case OverwriteAnswer::YesToAll: *answer = NOverwriteAnswer::kYesToAll; break;
    case OverwriteAnswer::No: *answer = NOverwriteAnswer::kNo; break;
    case OverwriteAnswer::NoToAll: *answer = NOverwriteAnswer::kNoToAll; break;
    case OverwriteAnswer::AutoRename: *answer = NOverwriteAnswer::kAutoRename; break;
    case OverwriteAnswer::Cancel:
    default:
      *answer = NOverwriteAnswer::kCancel;
      return E_ABORT;
  }
  return S_OK;
}

Z7_COM7F_IMF(ExtractBridge::PrepareOperation(
    const wchar_t *name, Int32 /*isFolder*/, Int32 /*askExtractMode*/, const UInt64 *position))
{
  if (m_progress)
  {
    m_progress->setCurrentFile(u2q(name));
    if (position)
      m_progress->setCompleted(*position);
  }
  return checkBreak();
}

Z7_COM7F_IMF(ExtractBridge::MessageError(const wchar_t *message))
{
  if (m_progress)
    m_progress->addMessage(u2q(message));
  return S_OK;
}

Z7_COM7F_IMF(ExtractBridge::SetOperationResult(Int32 /*opRes*/, Int32 /*encrypted*/))
{
  ++m_files;
  if (m_progress)
    m_progress->setCompleted(0, m_files);
  return checkBreak();
}

Z7_COM7F_IMF(ExtractBridge::ReportExtractResult(
    Int32 opRes, Int32 /*encrypted*/, const wchar_t *name))
{
  if (opRes != NArchive::NExtract::NOperationResult::kOK)
  {
    const QString s = QStringLiteral("Error extracting: %1").arg(u2q(name));
    m_messages.append(s);
    if (m_progress)
      m_progress->addMessage(s);
  }
  return S_OK;
}

Z7_COM7F_IMF(ExtractBridge::CryptoGetTextPassword(BSTR *password))
{
  if (!m_passwordDefined)
  {
    if (!m_prompts)
      return E_ABORT;
    QString pw;
    bool ok = false;
    m_prompts->askPassword(&pw, &ok, QStringLiteral("Password required"));
    if (!ok)
      return E_ABORT;
    m_password = q2u(pw);
    m_passwordDefined = true;
  }
  return StringToBstr(m_password, password);
}

class SimpleProgress final
    : public IFolderArchiveUpdateCallback,
      public IFolderArchiveUpdateCallback2,
      public IArchiveOpenCallback,
      public ICryptoGetTextPassword,
      public CMyUnknownImp
{
public:
  Z7_COM_QI_BEGIN2(IFolderArchiveUpdateCallback)
    Z7_COM_QI_ENTRY(IFolderArchiveUpdateCallback2)
    Z7_COM_QI_ENTRY(IArchiveOpenCallback)
    Z7_COM_QI_ENTRY(ICryptoGetTextPassword)
    Z7_COM_QI_ENTRY(IProgress)
  Z7_COM_QI_END
  Z7_COM_ADDREF_RELEASE
  Z7_IFACE_COM7_IMP(IFolderArchiveUpdateCallback)
  Z7_IFACE_COM7_IMP(IFolderArchiveUpdateCallback2)
  Z7_IFACE_COM7_IMP(IArchiveOpenCallback)
  Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)
  Z7_IFACE_COM7_IMP(IProgress)

public:
  SimpleProgress(ProgressState *progress, PromptDispatcher *prompts,
                 const UString &password, bool passwordDefined)
      : m_progress(progress), m_prompts(prompts), m_password(password),
        m_passwordDefined(passwordDefined) {}

private:
  HRESULT check() const { return (m_progress && m_progress->isCanceled()) ? E_ABORT : S_OK; }
  ProgressState *m_progress = nullptr;
  PromptDispatcher *m_prompts = nullptr;
  UString m_password;
  bool m_passwordDefined = false;
};

Z7_COM7F_IMF(SimpleProgress::SetTotal(UInt64 total))
{
  if (m_progress) m_progress->setTotals(total);
  return check();
}

Z7_COM7F_IMF(SimpleProgress::SetCompleted(const UInt64 *completed))
{
  if (m_progress && completed) m_progress->setCompleted(*completed);
  return check();
}

Z7_COM7F_IMF(SimpleProgress::SetTotal(const UInt64 *files, const UInt64 *bytes))
{
  if (m_progress) m_progress->setTotals(bytes ? *bytes : 0, files ? *files : 0);
  return check();
}

Z7_COM7F_IMF(SimpleProgress::SetCompleted(const UInt64 *files, const UInt64 *bytes))
{
  if (m_progress) m_progress->setCompleted(bytes ? *bytes : 0, files ? *files : 0);
  return check();
}

Z7_COM7F_IMF(SimpleProgress::CompressOperation(const wchar_t *name))
{
  if (m_progress) m_progress->setCurrentFile(u2q(name));
  return check();
}

Z7_COM7F_IMF(SimpleProgress::DeleteOperation(const wchar_t *name))
{
  if (m_progress) m_progress->setCurrentFile(QStringLiteral("Deleting %1").arg(u2q(name)));
  return check();
}

Z7_COM7F_IMF(SimpleProgress::OperationResult(Int32))
{
  if (m_progress) m_progress->setCompleted(0);
  return check();
}

Z7_COM7F_IMF(SimpleProgress::UpdateErrorMessage(const wchar_t *message))
{
  if (m_progress) m_progress->addMessage(u2q(message));
  return S_OK;
}

Z7_COM7F_IMF(SimpleProgress::SetNumFiles(UInt64 numFiles))
{
  if (m_progress) m_progress->setTotals(0, numFiles);
  return check();
}

Z7_COM7F_IMF(SimpleProgress::OpenFileError(const wchar_t *path, HRESULT errorCode))
{
  if (m_progress) m_progress->addMessage(QStringLiteral("Cannot open %1 (%2)").arg(u2q(path), hrText(errorCode)));
  return S_OK;
}

Z7_COM7F_IMF(SimpleProgress::ReadingFileError(const wchar_t *path, HRESULT errorCode))
{
  if (m_progress) m_progress->addMessage(QStringLiteral("Cannot read %1 (%2)").arg(u2q(path), hrText(errorCode)));
  return S_OK;
}

Z7_COM7F_IMF(SimpleProgress::ReportExtractResult(Int32, Int32, const wchar_t *))
{
  return S_OK;
}

Z7_COM7F_IMF(SimpleProgress::ReportUpdateOperation(UInt32, const wchar_t *name, Int32))
{
  if (m_progress) m_progress->setCurrentFile(u2q(name));
  return check();
}

Z7_COM7F_IMF(SimpleProgress::CryptoGetTextPassword(BSTR *password))
{
  if (!m_passwordDefined)
  {
    if (!m_prompts)
      return E_ABORT;
    QString pw;
    bool ok = false;
    m_prompts->askPassword(&pw, &ok, QStringLiteral("Archive password"));
    if (!ok)
      return E_ABORT;
    m_password = q2u(pw);
    m_passwordDefined = true;
  }
  return StringToBstr(m_password, password);
}

} // namespace

struct ArchiveSession::Impl
{
  CMyComPtr<IInFolderArchive> agent;
  CAgent *agentSpec = nullptr;
  CMyComPtr<IArchiveOpenCallback> openCallback;
  CMyComPtr<IInStream> stream;
  CMyComPtr<IFolderFolder> current;
  std::unique_ptr<OpenBridge> openBridge;
  QVector<CMyComPtr<IFolderFolder>> stack;
  QStringList stackNames;
  QString path;
  QString format;
  UString password;
  bool passwordDefined = false;
  bool open = false;

  CMyComPtr<IFolderFolder> rootFolder;
};

ArchiveSession::ArchiveSession() : d(std::make_unique<Impl>()) {}
ArchiveSession::~ArchiveSession() { close(); }

bool ArchiveSession::isOpen() const { return d->open; }
QString ArchiveSession::archivePath() const { return d->path; }
QString ArchiveSession::formatName() const { return d->format; }
QString ArchiveSession::password() const { return u2q(d->password); }

QString ArchiveSession::relativePath() const
{
  return d->stackNames.join(QLatin1Char('/'));
}

QString ArchiveSession::currentPath() const
{
  QString s = d->path;
  for (const QString &n : d->stackNames)
    s += QLatin1Char('/') + n;
  return s;
}

QStringList ArchiveSession::breadcrumbs() const
{
  QStringList out;
  out << d->path;
  for (const QString &n : d->stackNames)
    out << n;
  return out;
}

void ArchiveSession::close()
{
  d->current.Release();
  d->rootFolder.Release();
  d->stack.clear();
  d->stackNames.clear();
  d->openCallback.Release();
  d->agent.Release();
  d->agentSpec = nullptr;
  d->stream.Release();
  d->openBridge.reset();
  d->path.clear();
  d->format.clear();
  d->password.Empty();
  d->passwordDefined = false;
  d->open = false;
}

bool ArchiveSession::open(const QString &path, const QString &formatHint,
                          ProgressState *progress, PromptDispatcher *prompts,
                          QString *error, const QString &password)
{
  close();
  if (error)
    error->clear();

  if (!SevenZipEngine::instance().initialize(error))
    return false;

  const QFileInfo fi(path);
  if (!fi.exists() || !fi.isFile())
  {
    if (error)
      *error = QStringLiteral("File does not exist: %1").arg(path);
    return false;
  }

  d->path = fi.absoluteFilePath();
  d->password = q2u(password);
  d->passwordDefined = !password.isEmpty();
  d->openBridge = std::make_unique<OpenBridge>(progress, prompts, password);

  CInFileStream *streamSpec = new CInFileStream;
  CMyComPtr<IInStream> stream = streamSpec;
  if (!streamSpec->Open(q2fs(d->path)))
  {
    if (error)
      *error = QStringLiteral("Cannot open file: %1").arg(d->path);
    return false;
  }
  d->stream = stream;

  COpenCallbackImp *cbSpec = new COpenCallbackImp;
  CMyComPtr<IArchiveOpenCallback> cb = cbSpec;
  cbSpec->Callback = d->openBridge.get();
  const FString folder = q2fs(fi.absolutePath() + QLatin1Char('/'));
  const HRESULT initRes = cbSpec->Init2(folder, q2fs(fi.fileName()));
  if (initRes != S_OK)
  {
    if (error)
      *error = QStringLiteral("Cannot initialize open callback (%1)").arg(hrText(initRes));
    return false;
  }
  d->openCallback = cb;

  CAgent *agentSpec = new CAgent;
  CMyComPtr<IInFolderArchive> agent = agentSpec;
  const UString pathU = q2u(d->path);
  const UString hintU = q2u(formatHint);
  BSTR typeBstr = nullptr;
  HRESULT res = agent->Open(stream, pathU, hintU.IsEmpty() ? L"" : hintU.Ptr(), &typeBstr, cb);
  if (typeBstr)
  {
    d->format = u2q(typeBstr, SysStringLen(typeBstr));
    SysFreeString(typeBstr);
  }

  if (res != S_OK && res != S_FALSE)
  {
    QString msg;
    const UString agentMsg = agentSpec->GetErrorMessage();
    if (!agentMsg.IsEmpty())
      msg = u2q(agentMsg);
    if (error)
      *error = msg.isEmpty()
          ? QStringLiteral("Cannot open archive (%1)").arg(hrText(res))
          : msg;
    return false;
  }

  CMyComPtr<IFolderFolder> root;
  const HRESULT bindRes = agent->BindToRootFolder(&root);
  if (bindRes != S_OK || !root)
  {
    if (error)
      *error = QStringLiteral("Cannot read archive folder (%1)").arg(hrText(bindRes));
    return false;
  }

  if (!agentSpec->_proxy && !agentSpec->_proxy2)
  {
    if (error)
      *error = QStringLiteral("The archive could not be read (wrong password or unsupported archive)");
    return false;
  }

  d->agent = agent;
  d->agentSpec = agentSpec;
  d->rootFolder = root;
  d->current = root;
  d->open = true;
  if (d->openBridge && d->openBridge->passwordDefined())
  {
    d->password = d->openBridge->password();
    d->passwordDefined = true;
  }
  if (progress)
  {
    progress->setTitle(QStringLiteral("7zipLX"));
    progress->setStatus(QStringLiteral("Archive opened"));
  }
  return true;
}

int ArchiveSession::itemCount() const
{
  if (!d->open || !d->current || !d->agentSpec || (!d->agentSpec->_proxy && !d->agentSpec->_proxy2))
    return 0;
  UInt32 n = 0;
  if (d->current->GetNumberOfItems(&n) != S_OK)
    return 0;
  return int(n);
}

ArchiveEntryInfo ArchiveSession::item(int index) const
{
  ArchiveEntryInfo out;
  if (!d->open || !d->current || index < 0 || !d->agentSpec || (!d->agentSpec->_proxy && !d->agentSpec->_proxy2))
    return out;

  NCOM::CPropVariant prop;
  auto get = [&](PROPID id) -> PROPVARIANT * {
    prop.Clear();
    if (d->current->GetProperty(UInt32(index), id, &prop) == S_OK)
      return &prop;
    return nullptr;
  };

  if (PROPVARIANT *p = get(kpidName))
    out.name = propToString(*p);
  if (PROPVARIANT *p = get(kpidPath))
    out.path = propToString(*p);
  if (out.name.isEmpty())
    out.name = out.path;
  if (PROPVARIANT *p = get(kpidIsDir))
    out.isDir = propToBool(*p);
  if (PROPVARIANT *p = get(kpidSize))
    out.size = propToUInt64(*p);
  if (PROPVARIANT *p = get(kpidPackSize))
    out.packedSize = propToUInt64(*p);
  if (PROPVARIANT *p = get(kpidMTime); p && p->vt == VT_FILETIME)
    out.modified = fileTimeToDateTime(p->filetime);
  if (PROPVARIANT *p = get(kpidCTime); p && p->vt == VT_FILETIME)
    out.created = fileTimeToDateTime(p->filetime);
  if (PROPVARIANT *p = get(kpidATime); p && p->vt == VT_FILETIME)
    out.accessed = fileTimeToDateTime(p->filetime);
  if (PROPVARIANT *p = get(kpidAttrib))
    out.attrib = UInt32(propToUInt64(*p));
  if (PROPVARIANT *p = get(kpidCRC); p && (p->vt == VT_UI4 || p->vt == VT_UI8))
  {
    out.crc = UInt32(propToUInt64(*p));
    out.hasCrc = true;
  }
  if (PROPVARIANT *p = get(kpidEncrypted))
    out.isEncrypted = propToBool(*p);
  if (PROPVARIANT *p = get(kpidMethod))
    out.method = propToString(*p);
  if (PROPVARIANT *p = get(kpidType))
    out.type = propToString(*p);
  if (PROPVARIANT *p = get(kpidComment))
    out.comment = propToString(*p);
  if (PROPVARIANT *p = get(kpidIsAltStream))
    out.isAltStream = propToBool(*p);

  if (out.name.endsWith(QLatin1Char('/')))
    out.isDir = true;
  return out;
}

bool ArchiveSession::enter(int index)
{
  if (!d->open || !d->current || index < 0 || !d->agentSpec)
    return false;
  CMyComPtr<IFolderFolder> next;
  if (d->current->BindToFolder(UInt32(index), &next) != S_OK || !next)
    return false;
  ArchiveEntryInfo info = item(index);
  d->stack.append(d->current);
  d->stackNames.append(info.name);
  d->current = next;
  return true;
}

bool ArchiveSession::up()
{
  if (!d->open || d->stack.isEmpty())
    return false;
  d->current = d->stack.takeLast();
  if (!d->stackNames.isEmpty())
    d->stackNames.removeLast();
  return true;
}

bool ArchiveSession::setFlatMode(bool flat)
{
  if (!d->open || !d->current)
    return false;
  CMyComPtr<IFolderSetFlatMode> flatMode;
  if (d->current->QueryInterface(IID_IFolderSetFlatMode, (void **)&flatMode) != S_OK || !flatMode)
    return false;
  if (flatMode->SetFlatMode(flat ? 1 : 0) != S_OK)
    return false;
  if (d->current->LoadItems() != S_OK)
    return false;
  d->stack.clear();
  d->stackNames.clear();
  d->rootFolder = d->current;
  return true;
}

bool ArchiveSession::root()
{
  if (!d->open || !d->agent)
    return false;
  CMyComPtr<IFolderFolder> root;
  if (d->agent->BindToRootFolder(&root) != S_OK || !root)
    return false;
  d->current = root;
  d->stack.clear();
  d->stackNames.clear();
  return true;
}

OperationResult ArchiveSession::extract(const QVector<int> &selectedIndices,
                                       const QString &outputDir,
                                       bool testMode,
                                       ProgressState *progress,
                                       PromptDispatcher *prompts,
                                       const QString &password)
{
  ExtractOptions options;
  options.outputDir = outputDir;
  options.testMode = testMode;
  options.password = password;
  return extract(selectedIndices, options, progress, prompts);
}

OperationResult ArchiveSession::extract(const QVector<int> &selectedIndices,
                                       const ExtractOptions &options,
                                       ProgressState *progress,
                                       PromptDispatcher *prompts)
{
  const QString &outputDir = options.outputDir;
  const bool testMode = options.testMode;
  const QString &password = options.password;
  OperationResult result;
  if (!d->open || !d->current)
  {
    result.error = QStringLiteral("No archive is open");
    return result;
  }

  if (!testMode)
  {
    QDir dir;
    if (!outputDir.isEmpty() && !dir.mkpath(outputDir))
    {
      result.error = QStringLiteral("Cannot create output directory: %1").arg(outputDir);
      return result;
    }
  }

  CUIntVector indices;
  if (selectedIndices.isEmpty())
  {
    const int n = itemCount();
    indices.Reserve(n);
    for (int i = 0; i < n; ++i)
      indices.Add(UInt32(i));
  }
  else
  {
    indices.Reserve(selectedIndices.size());
    for (int i : selectedIndices)
      if (i >= 0 && i < itemCount())
        indices.Add(UInt32(i));
  }

  if (indices.IsEmpty())
  {
    result.error = QStringLiteral("No items selected");
    return result;
  }

  if (progress)
  {
    progress->setTitle(testMode ? QStringLiteral("Testing archive") : QStringLiteral("Extracting archive"));
    progress->setStatus(testMode ? QStringLiteral("Testing...") : QStringLiteral("Extracting..."));
  }

  ExtractBridge *bridgeSpec = new ExtractBridge(progress, prompts, password);
  CMyComPtr<IFolderArchiveExtractCallback> bridge = bridgeSpec;

  NExtract::NPathMode::EEnum pathMode = NExtract::NPathMode::kFullPaths;
  switch (options.pathMode)
  {
    case 1: pathMode = NExtract::NPathMode::kCurPaths; break;
    case 2: pathMode = NExtract::NPathMode::kNoPaths; break;
    case 3: pathMode = NExtract::NPathMode::kAbsPaths; break;
    default: pathMode = NExtract::NPathMode::kFullPaths; break;
  }
  NExtract::NOverwriteMode::EEnum overwriteMode = NExtract::NOverwriteMode::kAsk;
  switch (options.overwriteMode)
  {
    case 1: overwriteMode = NExtract::NOverwriteMode::kOverwrite; break;
    case 2: overwriteMode = NExtract::NOverwriteMode::kSkip; break;
    case 3: overwriteMode = NExtract::NOverwriteMode::kRename; break;
    case 4: overwriteMode = NExtract::NOverwriteMode::kRenameExisting; break;
    default: overwriteMode = NExtract::NOverwriteMode::kAsk; break;
  }

  CMyComPtr<IArchiveFolder> archiveFolder;
  if (d->current->QueryInterface(IID_IArchiveFolder, (void **)&archiveFolder) != S_OK || !archiveFolder)
  {
    result.error = QStringLiteral("Archive folder does not support extraction");
    return result;
  }

  const UString outDirU = q2u(testMode ? QString() : outputDir);
  const HRESULT res = archiveFolder->Extract(
      indices.ConstData(), indices.Size(),
      options.includeAltStreams ? 1 : 0, 0,
      pathMode,
      overwriteMode,
      outDirU.IsEmpty() ? L"" : outDirU.Ptr(),
      testMode ? 1 : 0,
      bridge);

  result.messages = bridgeSpec->messages();
  result.files = bridgeSpec->files();
  if (res == E_ABORT)
  {
    result.canceled = true;
    result.error = QStringLiteral("Operation canceled");
  }
  else if (res != S_OK)
  {
    result.error = QStringLiteral("Extraction failed (%1)").arg(hrText(res));
  }
  else
  {
    result.ok = true;
  }

  if (progress)
  {
    progress->setStatus(result.canceled ? QStringLiteral("Canceled")
                                        : (result.ok ? QStringLiteral("Finished") : QStringLiteral("Failed")));
    for (const QString &m : result.messages)
      progress->addMessage(m);
  }
  return result;
}


bool ArchiveSession::rebindAfterEdit(const QStringList &pathNames)
{
  d->stack.clear();
  d->stackNames.clear();
  d->current.Release();
  d->rootFolder.Release();
  if (!d->agent)
  {
    d->open = false;
    return false;
  }
  CMyComPtr<IFolderFolder> root;
  if (d->agent->BindToRootFolder(&root) != S_OK || !root)
  {
    d->open = false;
    return false;
  }
  d->rootFolder = root;
  d->current = root;
  for (const QString &name : pathNames)
  {
    if (name.isEmpty())
      continue;
    int found = -1;
    const int count = itemCount();
    for (int i = 0; i < count; ++i)
    {
      const ArchiveEntryInfo entry = item(i);
      if (entry.isDir && entry.name == name)
      {
        found = i;
        break;
      }
    }
    if (found < 0)
      break;
    if (!enter(found))
      break;
  }
  return true;
}

OperationResult ArchiveSession::deleteItems(const QVector<int> &selectedIndices,
                                           ProgressState *progress,
                                           PromptDispatcher *prompts)
{
  OperationResult result;
  if (!d->open || !d->current)
  {
    result.error = QStringLiteral("No archive is open");
    return result;
  }
  if (selectedIndices.isEmpty())
  {
    result.error = QStringLiteral("No items selected");
    return result;
  }
  CMyComPtr<IFolderOperations> ops;
  if (d->current->QueryInterface(IID_IFolderOperations, (void **)&ops) != S_OK || !ops)
  {
    result.error = QStringLiteral("This archive does not support editing");
    return result;
  }
  CUIntVector indices;
  for (int i : selectedIndices)
    if (i >= 0 && i < itemCount())
      indices.Add(UInt32(i));
  if (indices.IsEmpty())
  {
    result.error = QStringLiteral("No items selected");
    return result;
  }
  const QStringList pathNames = d->stackNames;
  SimpleProgress *progressSpec = new SimpleProgress(progress, prompts, d->password, d->passwordDefined);
  CMyComPtr<IProgress> progressPtr = progressSpec;
  if (progress)
  {
    progress->setTitle(QStringLiteral("Updating archive"));
    progress->setStatus(QStringLiteral("Deleting items..."));
  }
  const HRESULT res = ops->Delete(indices.ConstData(), indices.Size(), progressPtr);
  const bool rebound = rebindAfterEdit(pathNames);
  if (res == E_ABORT)
  {
    result.canceled = true;
    result.error = QStringLiteral("Operation canceled");
  }
  else if (res != S_OK)
  {
    result.error = QStringLiteral("Cannot delete items (%1)").arg(hrText(res));
  }
  else if (!rebound)
  {
    result.error = QStringLiteral("The archive was modified but could not be reopened");
  }
  else
  {
    result.ok = true;
  }
  return result;
}

OperationResult ArchiveSession::setComment(int index, const QString &comment,
                                           ProgressState *progress, PromptDispatcher *prompts)
{
  OperationResult result;
  if (!d->open || !d->current || index < 0)
  {
    result.error = QStringLiteral("Invalid selection");
    return result;
  }
  CMyComPtr<IFolderOperations> ops;
  if (d->current->QueryInterface(IID_IFolderOperations, (void **)&ops) != S_OK || !ops)
  {
    result.error = QStringLiteral("This archive does not support comments");
    return result;
  }
  const QStringList pathNames = d->stackNames;
  SimpleProgress *progressSpec = new SimpleProgress(progress, prompts, d->password, d->passwordDefined);
  CMyComPtr<IProgress> progressPtr = progressSpec;
  if (progress)
  {
    progress->setTitle(QStringLiteral("Updating archive"));
    progress->setStatus(QStringLiteral("Saving comment..."));
  }
  NCOM::CPropVariant prop;
  prop = q2u(comment);
  const HRESULT res = ops->SetProperty(UInt32(index), kpidComment, &prop, progressPtr);
  const bool rebound = rebindAfterEdit(pathNames);
  if (res == E_ABORT)
  {
    result.canceled = true;
    result.error = QStringLiteral("Operation canceled");
  }
  else if (res != S_OK)
  {
    result.error = QStringLiteral("Cannot save comment (%1)").arg(hrText(res));
  }
  else if (!rebound)
  {
    result.error = QStringLiteral("The archive was modified but could not be reopened");
  }
  else
  {
    result.ok = true;
  }
  return result;
}

OperationResult ArchiveSession::renameItem(int index, const QString &newName,
                                           ProgressState *progress,
                                           PromptDispatcher *prompts)
{
  OperationResult result;
  if (!d->open || !d->current || index < 0 || newName.isEmpty())
  {
    result.error = QStringLiteral("Invalid selection");
    return result;
  }
  CMyComPtr<IFolderOperations> ops;
  if (d->current->QueryInterface(IID_IFolderOperations, (void **)&ops) != S_OK || !ops)
  {
    result.error = QStringLiteral("This archive does not support editing");
    return result;
  }
  const QStringList pathNames = d->stackNames;
  SimpleProgress *progressSpec = new SimpleProgress(progress, prompts, d->password, d->passwordDefined);
  CMyComPtr<IProgress> progressPtr = progressSpec;
  if (progress)
  {
    progress->setTitle(QStringLiteral("Updating archive"));
    progress->setStatus(QStringLiteral("Renaming item..."));
  }
  const HRESULT res = ops->Rename(UInt32(index), q2u(newName), progressPtr);
  const bool rebound = rebindAfterEdit(pathNames);
  if (res == E_ABORT)
  {
    result.canceled = true;
    result.error = QStringLiteral("Operation canceled");
  }
  else if (res != S_OK)
  {
    result.error = QStringLiteral("Cannot rename item (%1)").arg(hrText(res));
  }
  else if (!rebound)
  {
    result.error = QStringLiteral("The archive was modified but could not be reopened");
  }
  else
  {
    result.ok = true;
  }
  return result;
}

OperationResult ArchiveSession::createFolder(const QString &name, ProgressState *progress,
                                             PromptDispatcher *prompts)
{
  OperationResult result;
  if (!d->open || !d->current || name.isEmpty())
  {
    result.error = QStringLiteral("Invalid folder name");
    return result;
  }
  CMyComPtr<IFolderOperations> ops;
  if (d->current->QueryInterface(IID_IFolderOperations, (void **)&ops) != S_OK || !ops)
  {
    result.error = QStringLiteral("This archive does not support editing");
    return result;
  }
  const QStringList pathNames = d->stackNames;
  SimpleProgress *progressSpec = new SimpleProgress(progress, prompts, d->password, d->passwordDefined);
  CMyComPtr<IProgress> progressPtr = progressSpec;
  if (progress)
  {
    progress->setTitle(QStringLiteral("Updating archive"));
    progress->setStatus(QStringLiteral("Creating folder..."));
  }
  const HRESULT res = ops->CreateFolder(q2u(name), progressPtr);
  const bool rebound = rebindAfterEdit(pathNames);
  if (res == E_ABORT)
  {
    result.canceled = true;
    result.error = QStringLiteral("Operation canceled");
  }
  else if (res != S_OK)
  {
    result.error = QStringLiteral("Cannot create folder (%1)").arg(hrText(res));
  }
  else if (!rebound)
  {
    result.error = QStringLiteral("The archive was modified but could not be reopened");
  }
  else
  {
    result.ok = true;
  }
  return result;
}
