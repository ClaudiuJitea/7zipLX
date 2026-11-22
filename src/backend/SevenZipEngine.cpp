#include "SevenZipEngine.h"
#include "Unicode.h"

#include "Common/MyCom.h"
#include "7zip/UI/Agent/Agent.h"
#include "7zip/UI/Common/LoadCodecs.h"
#include <QSet>

#include <QCoreApplication>
#include <QFile>

extern void Set_ModuleDirPrefix_From_ProgArg0(const char *s);


#ifdef Z7_EXTERNAL_CODECS
extern const CExternalCodecs *g_ExternalCodecs_Ptr;

const CExternalCodecs *SevenZipEngine::externalCodecs() const
{
  return g_ExternalCodecs_Ptr;
}

int SevenZipEngine::externalCodecModuleCount() const
{
  return g_CodecsObj ? int(g_CodecsObj->Libs.Size()) : 0;
}
#endif

SevenZipEngine &SevenZipEngine::instance()
{
  static SevenZipEngine engine;
  return engine;
}

bool SevenZipEngine::initialize(QString *error)
{
  if (m_initialized)
    return true;
  if (QCoreApplication::instance())
    Set_ModuleDirPrefix_From_ProgArg0(QCoreApplication::applicationFilePath().toUtf8().constData());
  const HRESULT res = ::LoadGlobalCodecs();
  if (res != S_OK || !g_CodecsObj)
  {
    if (error)
      *error = QStringLiteral("Cannot load 7-Zip codecs (0x%1)").arg(quint32(res), 8, 16, QLatin1Char('0'));
    return false;
  }
  m_initialized = true;
  return true;
}

void SevenZipEngine::shutdown()
{
  if (!m_initialized)
    return;
  ::FreeGlobalCodecs();
  m_initialized = false;
}

QStringList SevenZipEngine::archiveFormats() const
{
  QStringList out;
  QSet<QString> seen;
  if (!g_CodecsObj)
    return out;
  for (unsigned i = 0; i < g_CodecsObj->Formats.Size(); ++i)
  {
    const QString name = u2q(g_CodecsObj->Formats[i].Name);
    if (!seen.contains(name))
    {
      seen.insert(name);
      out << name;
    }
  }
  return out;
}

QStringList SevenZipEngine::updateFormats() const
{
  QStringList out;
  QSet<QString> seen;
  if (!g_CodecsObj)
    return out;
  for (unsigned i = 0; i < g_CodecsObj->Formats.Size(); ++i)
  {
    const CArcInfoEx &format = g_CodecsObj->Formats[i];
    if (!format.UpdateEnabled)
      continue;
    const QString name = u2q(format.Name);
    if (!seen.contains(name))
    {
      seen.insert(name);
      out << name;
    }
  }
  return out;
}

int SevenZipEngine::formatIndex(const QString &name) const
{
  if (!g_CodecsObj)
    return -1;
  for (unsigned i = 0; i < g_CodecsObj->Formats.Size(); ++i)
    if (u2q(g_CodecsObj->Formats[i].Name).compare(name, Qt::CaseInsensitive) == 0)
      return int(i);
  return -1;
}

QString SevenZipEngine::formatName(int index) const
{
  if (!g_CodecsObj || index < 0 || unsigned(index) >= g_CodecsObj->Formats.Size())
    return QString();
  return u2q(g_CodecsObj->Formats[unsigned(index)].Name);
}

QString SevenZipEngine::formatExtension(int index) const
{
  if (!g_CodecsObj || index < 0 || unsigned(index) >= g_CodecsObj->Formats.Size())
    return QString();
  return u2q(g_CodecsObj->Formats[unsigned(index)].GetMainExt());
}
