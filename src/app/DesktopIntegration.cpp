#include "DesktopIntegration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QVector>

namespace {

const char kMarker[] = "# 7zipLX-managed-script v2";

} // namespace

namespace DesktopIntegration {

const QVector<Script> &fileManagerScripts()
{
  static const QVector<Script> scripts = {
      {QStringLiteral("Open in 7zipLX"), QString()},
      {QStringLiteral("Extract Here"), QStringLiteral("--extract-here")},
      {QStringLiteral("Extract to Folder"), QStringLiteral("--extract-to-folder")},
      {QStringLiteral("Extract Files…"), QStringLiteral("--extract")},
      {QStringLiteral("Test Archive"), QStringLiteral("--test")},
      {QStringLiteral("Add to Archive…"), QStringLiteral("--add")},
      {QStringLiteral("Compress to 7z"), QStringLiteral("--compress --format 7z")},
      {QStringLiteral("Compress to zip"), QStringLiteral("--compress --format zip")},
  };
  return scripts;
}

QString scriptsDirectory()
{
  return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
         QStringLiteral("/nautilus/scripts/7zipLX");
}

QString scriptContents(const Script &script, const QString &program)
{
  QString body;
  body += QStringLiteral("#!/bin/sh\n");
  body += QString::fromLatin1(kMarker) + QLatin1Char('\n');
  body += QStringLiteral("# Installed by 7zipLX (Options > Integration). Edits are overwritten.\n");
  body += QStringLiteral("# Nautilus and the desktop pass the selection in this variable, one path\n"
                         "# per line; fall back to the arguments for other launchers.\n");
  body += QStringLiteral("if [ -n \"$NAUTILUS_SCRIPT_SELECTED_FILE_PATHS\" ]; then\n"
                         "  set -f\n"
                         "  IFS='\n'\n"
                         "  set -- $NAUTILUS_SCRIPT_SELECTED_FILE_PATHS\n"
                         "  unset IFS\n"
                         "  set +f\n"
                         "fi\n");
  if (script.arguments.isEmpty())
  {
    // Open each selected archive (or folder) in its own window.
    body += QStringLiteral("for item in \"$@\"; do\n  %1 \"$item\" &\ndone\n").arg(program);
  }
  else
  {
    body += QStringLiteral("exec %1 %2 \"$@\"\n").arg(program, script.arguments);
  }
  return body;
}

static bool isManaged(const QString &path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return false;
  const QByteArray head = file.read(256);
  return head.contains("# 7zipLX-managed-script");
}

bool syncFileManagerScripts(bool enabled, QString *error, const QString &program)
{
  const QString dir = scriptsDirectory();
  QDir scripts(dir);
  // Remove our own files that are no longer wanted (or renamed in an update).
  QStringList wanted;
  if (enabled)
    for (const Script &script : fileManagerScripts())
      wanted << script.fileName;
  if (scripts.exists())
  {
    for (const QString &name : scripts.entryList(QDir::Files | QDir::Hidden))
      if (!wanted.contains(name) && isManaged(scripts.filePath(name)))
        QFile::remove(scripts.filePath(name));
  }
  if (!enabled)
  {
    if (scripts.exists() && scripts.isEmpty())
      QDir().rmdir(dir);
    return true;
  }
  if (!QDir().mkpath(dir))
  {
    if (error)
      *error = QStringLiteral("Cannot create %1").arg(dir);
    return false;
  }
  for (const Script &script : fileManagerScripts())
  {
    const QString path = scripts.filePath(script.fileName);
    // A file of the same name that the user wrote is left alone.
    if (QFileInfo::exists(path) && !isManaged(path))
      continue;
    const QByteArray contents = scriptContents(script, program).toUtf8();
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly) && existing.readAll() == contents)
    {
      existing.close();
    }
    else
    {
      existing.close();
      QFile out(path);
      if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(contents) != contents.size())
      {
        if (error)
          *error = QStringLiteral("Cannot write %1").arg(path);
        return false;
      }
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                    QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                                    QFileDevice::ReadOther | QFileDevice::ExeOther);
  }
  return true;
}

} // namespace DesktopIntegration
