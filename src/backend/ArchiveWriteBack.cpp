#include "ArchiveWriteBack.h"

#include "CompressOperation.h"
#include "ProgressState.h"
#include "SevenZipEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace {

QString uniqueSiblingPath(const QString &archivePath, const QString &tag)
{
  QString candidate = archivePath + tag;
  for (int i = 2; QFile::exists(candidate) || QFileInfo::exists(candidate); ++i)
    candidate = archivePath + QStringLiteral("%1-%2").arg(tag).arg(i);
  return candidate;
}

OperationResult rebuildSingleFileArchive(const QString &archivePath, const QString &replacementFile,
                                         const QString &formatName, const QString &password,
                                         ProgressState *progress, PromptDispatcher *prompts)
{
  OperationResult result;
  const QFileInfo original(archivePath);
  if (!original.exists() || !original.isFile())
  {
    result.error = QStringLiteral("Archive does not exist: %1").arg(archivePath);
    return result;
  }

  const QString newPath = uniqueSiblingPath(archivePath, QStringLiteral(".7zlx-new"));
  const QString backupPath = uniqueSiblingPath(archivePath, QStringLiteral(".7zlx-bak"));
  const QFile::Permissions permissions = original.permissions();

  CompressOptions options;
  options.archivePath = newPath;
  options.formatIndex = SevenZipEngine::instance().formatIndex(formatName);
  options.password = password;
  options.level = 5;
  if (progress)
  {
    progress->setTitle(QStringLiteral("Saving nested archive"));
    progress->setStatus(QStringLiteral("Rebuilding %1").arg(original.fileName()));
  }
  result = compressFiles({replacementFile}, options, progress, prompts);
  if (!result.ok)
  {
    QFile::remove(newPath);
    return result;
  }
  if (!QFileInfo(newPath).isFile() || QFileInfo(newPath).size() == 0)
  {
    QFile::remove(newPath);
    result.ok = false;
    result.error = QStringLiteral("The rebuilt archive was not created.");
    return result;
  }

  if (!QFile::rename(archivePath, backupPath))
  {
    QFile::remove(newPath);
    result.ok = false;
    result.error = QStringLiteral("Cannot replace archive: %1").arg(archivePath);
    return result;
  }
  if (!QFile::rename(newPath, archivePath))
  {
    QFile::rename(backupPath, archivePath);
    QFile::remove(newPath);
    result.ok = false;
    result.error = QStringLiteral("Cannot install the rebuilt archive: %1").arg(archivePath);
    return result;
  }

  QFile::setPermissions(archivePath, permissions);
  QFile::remove(backupPath);
  return result;
}

} // namespace

bool isSplitVolumePath(const QString &path)
{
  const QString suffix = QFileInfo(path).suffix();
  return suffix.size() == 3 &&
         suffix.at(0) == QLatin1Char('0') &&
         suffix.at(1) == QLatin1Char('0') &&
         suffix.at(2).isDigit();
}

bool isSingleFileCompressorFormat(const QString &formatName)
{
  const QString f = formatName.toLower();
  return f == QLatin1String("gzip") || f == QLatin1String("tgzip") ||
         f == QLatin1String("bzip2") || f == QLatin1String("tbz2") ||
         f == QLatin1String("xz") || f == QLatin1String("txz") ||
         f == QLatin1String("zstd") || f == QLatin1String("tzstd") ||
         f == QLatin1String("lzma") || f == QLatin1String("lzma2") ||
         f == QLatin1String("compress") || f == QLatin1String("z");
}

bool canReplaceArchiveEntry(const QString &archivePath, const QString &archiveSubdir,
                            const QString &formatName, QString *reason)
{
  if (archivePath.isEmpty() || !QFileInfo(archivePath).isFile())
  {
    if (reason) *reason = QStringLiteral("The parent archive is not available.");
    return false;
  }
  if (isSplitVolumePath(archivePath))
  {
    if (reason)
      *reason = QStringLiteral("Writing inside split-volume archives is not supported. "
                               "Extract the inner archive first, then open it from disk.");
    return false;
  }
  const QStringList updateFormats = SevenZipEngine::instance().updateFormats();
  if (!updateFormats.contains(formatName, Qt::CaseInsensitive))
  {
    if (reason)
      *reason = QStringLiteral("The parent archive format (%1) cannot be updated by the "
                               "7-Zip engine.").arg(formatName.isEmpty() ? QStringLiteral("unknown") : formatName);
    return false;
  }
  if (isSingleFileCompressorFormat(formatName) && !archiveSubdir.isEmpty())
  {
    if (reason)
      *reason = QStringLiteral("A single-file %1 archive cannot contain folders.").arg(formatName);
    return false;
  }
  return true;
}

OperationResult replaceArchiveEntry(const QString &archivePath, const QString &archiveSubdir,
                                    const QString &replacementFile, const QString &formatName,
                                    const QString &password, ProgressState *progress,
                                    PromptDispatcher *prompts)
{
  OperationResult result;
  if (!QFileInfo(replacementFile).isFile())
  {
    result.error = QStringLiteral("Replacement file does not exist: %1").arg(replacementFile);
    return result;
  }
  QString reason;
  if (!canReplaceArchiveEntry(archivePath, archiveSubdir, formatName, &reason))
  {
    result.error = reason;
    return result;
  }

  if (isSingleFileCompressorFormat(formatName))
    return rebuildSingleFileArchive(archivePath, replacementFile, formatName, password,
                                    progress, prompts);

  CompressOptions options;
  options.archivePath = archivePath;
  options.archiveSubdir = archiveSubdir;
  options.formatIndex = SevenZipEngine::instance().formatIndex(formatName);
  options.password = password;
  options.level = 5;
  if (progress)
  {
    progress->setTitle(QStringLiteral("Saving nested archive"));
    progress->setStatus(QStringLiteral("Updating %1").arg(QFileInfo(archivePath).fileName()));
  }
  return compressFiles({replacementFile}, options, progress, prompts);
}
