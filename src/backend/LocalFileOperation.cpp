#include "LocalFileOperation.h"

#include "PromptDispatcher.h"
#include "ProgressState.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace {

struct ConflictState
{
  bool overwriteAll = false;
  bool skipAll = false;
};

enum class Decision
{
  Overwrite,
  Skip,
  Rename,
  Cancel
};

QString uniqueDestination(const QString &path)
{
  const QFileInfo info(path);
  const QDir dir = info.absoluteDir();
  const QString name = info.fileName();
  const bool isDir = info.isDir();
  QString base = name;
  QString suffix;
  if (!isDir)
  {
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0)
    {
      base = name.left(dot);
      suffix = name.mid(dot);
    }
  }
  for (int i = 2; i < 10000; ++i)
  {
    const QString candidate = dir.absoluteFilePath(
        QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(suffix));
    if (!QFileInfo::exists(candidate))
      return candidate;
  }
  return dir.absoluteFilePath(name + QStringLiteral(".copy"));
}

Decision askDecision(const QFileInfo &source, const QFileInfo &destination,
                     ConflictState &state, PromptDispatcher *prompts)
{
  if (state.overwriteAll)
    return Decision::Overwrite;
  if (state.skipAll)
    return Decision::Skip;
  if (!prompts)
    return Decision::Overwrite;

  OverwriteInfo info;
  info.existingName = destination.fileName();
  info.newName = source.fileName();
  info.existingTime = destination.lastModified();
  info.newTime = source.lastModified();
  info.existingSize = quint64(destination.size());
  info.newSize = quint64(source.size());
  info.existingSizeKnown = destination.isFile();
  info.newSizeKnown = source.isFile();

  switch (prompts->askOverwrite(info))
  {
    case OverwriteAnswer::Yes:
      return Decision::Overwrite;
    case OverwriteAnswer::YesToAll:
      state.overwriteAll = true;
      return Decision::Overwrite;
    case OverwriteAnswer::No:
      return Decision::Skip;
    case OverwriteAnswer::NoToAll:
      state.skipAll = true;
      return Decision::Skip;
    case OverwriteAnswer::AutoRename:
      return Decision::Rename;
    case OverwriteAnswer::Cancel:
    default:
      return Decision::Cancel;
  }
}

bool copyEntry(const QString &sourcePath, const QString &destinationPath,
               ConflictState &conflicts, ProgressState *progress,
               PromptDispatcher *prompts, OperationResult &result,
               bool &skipped)
{
  if (progress && progress->isCanceled())
  {
    result.canceled = true;
    return false;
  }

  const QFileInfo source(sourcePath);
  QFileInfo destination(destinationPath);
  QString target = destinationPath;

  if (source.isSymLink())
  {
    if (QFileInfo::exists(target) || destination.isSymLink())
    {
      const Decision decision = askDecision(source, destination, conflicts, prompts);
      if (decision == Decision::Cancel)
      {
        result.canceled = true;
        return false;
      }
      if (decision == Decision::Skip)
      {
        skipped = true;
        return true;
      }
      if (decision == Decision::Rename)
        target = uniqueDestination(target);
      else if (!QFile::remove(target))
      {
        result.error = QStringLiteral("Cannot replace: %1").arg(target);
        return false;
      }
    }
    if (!QFile::link(source.symLinkTarget(), target))
    {
      result.error = QStringLiteral("Cannot create link: %1").arg(target);
      return false;
    }
    ++result.files;
    if (progress)
    {
      progress->setCurrentFile(source.fileName());
      progress->setCompleted(0, result.files);
    }
    return true;
  }

  if (source.isDir())
  {
    if (QFileInfo::exists(target) && !QFileInfo(target).isDir())
    {
      const Decision decision = askDecision(source, QFileInfo(target), conflicts, prompts);
      if (decision == Decision::Cancel)
      {
        result.canceled = true;
        return false;
      }
      if (decision == Decision::Skip)
      {
        skipped = true;
        return true;
      }
      if (decision == Decision::Rename)
        target = uniqueDestination(target);
      else if (!QFile::remove(target))
      {
        result.error = QStringLiteral("Cannot replace: %1").arg(target);
        return false;
      }
    }
    if (!QDir().mkpath(target))
    {
      result.error = QStringLiteral("Cannot create directory: %1").arg(target);
      return false;
    }
    QDirIterator it(sourcePath, QDir::AllEntries | QDir::NoDotAndDotDot |
                                     QDir::Hidden | QDir::System,
                    QDirIterator::NoIteratorFlags);
    while (it.hasNext())
    {
      const QString child = it.next();
      const QString childTarget =
          target + QLatin1Char('/') + QFileInfo(child).fileName();
      if (!copyEntry(child, childTarget, conflicts, progress, prompts, result, skipped))
        return false;
    }
    return true;
  }

  if (QFileInfo::exists(target))
  {
    const Decision decision = askDecision(source, QFileInfo(target), conflicts, prompts);
    if (decision == Decision::Cancel)
    {
      result.canceled = true;
      return false;
    }
    if (decision == Decision::Skip)
    {
      skipped = true;
      return true;
    }
    if (decision == Decision::Rename)
      target = uniqueDestination(target);
    else if (!QFile::remove(target))
    {
      result.error = QStringLiteral("Cannot replace: %1").arg(target);
      return false;
    }
  }

  if (!QFile::copy(sourcePath, target))
  {
    result.error = QStringLiteral("Cannot copy: %1").arg(sourcePath);
    return false;
  }

  // Preserve the modification time and permissions where possible.
  {
    QFile copied(target);
    if (copied.open(QIODevice::ReadWrite))
    {
      copied.setFileTime(source.lastModified(), QFileDevice::FileModificationTime);
      copied.setPermissions(source.permissions());
    }
  }

  ++result.files;
  if (progress)
  {
    progress->setCurrentFile(source.fileName());
    progress->setCompleted(0, result.files);
  }
  return true;
}

} // namespace

OperationResult copyLocalItems(const QStringList &sources, const QString &targetDir,
                               bool move, ProgressState *progress, PromptDispatcher *prompts)
{
  OperationResult result;
  if (sources.isEmpty())
  {
    result.error = QStringLiteral("No items selected");
    return result;
  }
  const QFileInfo targetInfo(targetDir);
  if (!targetInfo.isDir())
  {
    result.error = QStringLiteral("Destination folder does not exist: %1").arg(targetDir);
    return result;
  }
  const QString targetRoot = targetInfo.absoluteFilePath();

  if (progress)
  {
    progress->setTitle(move ? QStringLiteral("Moving") : QStringLiteral("Copying"));
    progress->setStatus(QStringLiteral("Preparing..."));
    quint64 totalBytes = 0;
    quint64 totalFiles = 0;
    for (const QString &source : sources)
    {
      const QFileInfo info(source);
      if (info.isFile() && !info.isSymLink())
        totalBytes += quint64(info.size());
      ++totalFiles;
    }
    progress->setTotals(totalBytes, totalFiles);
  }

  ConflictState conflicts;
  QStringList movedSources;
  QStringList keptSources;
  for (const QString &sourcePath : sources)
  {
    if (progress && progress->isCanceled())
    {
      result.canceled = true;
      break;
    }
    const QFileInfo source(sourcePath);
    if (!source.exists() && !source.isSymLink())
    {
      result.error = QStringLiteral("Source does not exist: %1").arg(sourcePath);
      break;
    }
    const QString destination =
        targetRoot + QLatin1Char('/') + source.fileName();
    const bool samePath =
        QFileInfo(destination).absoluteFilePath() == source.absoluteFilePath();
    if (samePath)
      continue;
    if ((targetRoot + QLatin1Char('/')).startsWith(source.absoluteFilePath() + QLatin1Char('/')))
    {
      result.error = QStringLiteral("Cannot copy or move a folder into itself: %1").arg(sourcePath);
      break;
    }

    bool skipped = false;
    if (!copyEntry(source.absoluteFilePath(), destination, conflicts, progress,
                   prompts, result, skipped))
      break;
    if (move)
    {
      if (skipped)
        keptSources.append(sourcePath);
      else
        movedSources.append(sourcePath);
    }
  }

  if (result.error.isEmpty() && !result.canceled && move)
  {
    for (const QString &sourcePath : movedSources)
    {
      const QFileInfo source(sourcePath);
      bool removed = false;
      if (source.isSymLink() || !source.isDir())
        removed = QFile::remove(sourcePath);
      else
        removed = QDir(sourcePath).removeRecursively();
      if (!removed)
      {
        result.error = QStringLiteral("Cannot remove source: %1").arg(sourcePath);
        break;
      }
    }
  }

  if (!keptSources.isEmpty())
    result.messages.append(QStringLiteral("Source kept because items were skipped: %1")
                               .arg(keptSources.join(QStringLiteral(", "))));

  if (result.canceled)
  {
    result.ok = false;
    result.error = QStringLiteral("Operation canceled");
  }
  else if (!result.error.isEmpty())
  {
    result.ok = false;
  }
  else
  {
    result.ok = true;
  }

  if (progress)
  {
    progress->setStatus(result.ok ? QStringLiteral("Finished")
                                  : (result.canceled ? QStringLiteral("Canceled")
                                                     : QStringLiteral("Failed")));
    for (const QString &message : result.messages)
      progress->addMessage(message);
  }
  return result;
}

namespace {

constexpr qint64 kStreamBuffer = 1 << 20;

bool streamCopy(QFile &in, QFile &out, qint64 limit, ProgressState *progress, quint64 *done,
                QString *error)
{
  QByteArray buffer;
  qint64 remaining = limit;
  while (remaining != 0 && !in.atEnd())
  {
    if (progress && progress->isCanceled())
      return false;
    const qint64 want = remaining < 0 ? kStreamBuffer : qMin(kStreamBuffer, remaining);
    buffer = in.read(want);
    if (buffer.isEmpty())
    {
      if (in.error() != QFileDevice::NoError)
      {
        *error = QObject::tr("Cannot read %1: %2").arg(in.fileName(), in.errorString());
        return false;
      }
      break;
    }
    if (out.write(buffer) != buffer.size())
    {
      *error = QObject::tr("Cannot write %1: %2").arg(out.fileName(), out.errorString());
      return false;
    }
    if (remaining > 0)
      remaining -= buffer.size();
    *done += quint64(buffer.size());
    if (progress)
      progress->setCompleted(*done);
  }
  return true;
}

} // namespace

QString splitVolumeName(const QString &source, int part)
{
  return source + QLatin1Char('.') + QStringLiteral("%1").arg(part, 3, 10, QLatin1Char('0'));
}

int splitVolumeCount(qint64 fileSize, qint64 volumeSize)
{
  if (volumeSize <= 0)
    return 1;
  return int(qMax<qint64>(1, (fileSize + volumeSize - 1) / volumeSize));
}

OperationResult splitFile(const QString &source, qint64 volumeSize, ProgressState *progress)
{
  OperationResult result;
  const QFileInfo info(source);
  if (!info.isFile())
  {
    result.error = QObject::tr("Select one file to split.");
    return result;
  }
  if (volumeSize <= 0)
  {
    result.error = QObject::tr("The volume size must be greater than zero.");
    return result;
  }
  const int parts = splitVolumeCount(info.size(), volumeSize);
  if (parts > 999)
  {
    result.error = QObject::tr("This would create %1 volumes; choose a larger volume size.").arg(parts);
    return result;
  }
  for (int part = 1; part <= parts; ++part)
  {
    if (QFileInfo::exists(splitVolumeName(source, part)))
    {
      result.error = QObject::tr("%1 already exists.").arg(QFileInfo(splitVolumeName(source, part)).fileName());
      return result;
    }
  }
  QFile in(source);
  if (!in.open(QIODevice::ReadOnly))
  {
    result.error = QObject::tr("Cannot open %1: %2").arg(source, in.errorString());
    return result;
  }
  if (progress)
  {
    progress->setStatus(QObject::tr("Splitting %1").arg(info.fileName()));
    progress->setTotals(quint64(info.size()), quint64(parts));
  }
  quint64 done = 0;
  QStringList written;
  for (int part = 1; part <= parts; ++part)
  {
    const QString name = splitVolumeName(source, part);
    QFile out(name);
    if (!out.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    {
      result.error = QObject::tr("Cannot create %1: %2").arg(name, out.errorString());
      break;
    }
    written << name;
    if (progress)
      progress->setCurrentFile(QFileInfo(name).fileName());
    if (!streamCopy(in, out, volumeSize, progress, &done, &result.error))
      break;
    ++result.files;
    if (progress)
      progress->setCompleted(done, quint64(part));
  }
  if (result.files != quint64(parts))
  {
    result.canceled = result.error.isEmpty();
    for (const QString &name : written)
      QFile::remove(name); // never leave a partial set behind
    result.files = 0;
    return result;
  }
  result.ok = true;
  return result;
}

OperationResult combineFiles(const QString &firstVolume, const QString &output, ProgressState *progress)
{
  OperationResult result;
  if (!firstVolume.endsWith(QStringLiteral(".001")))
  {
    result.error = QObject::tr("Select the first volume (.001) to combine.");
    return result;
  }
  const QString base = firstVolume.left(firstVolume.size() - 4);
  QStringList volumes;
  quint64 total = 0;
  for (int part = 1; part <= 999; ++part)
  {
    const QString name = splitVolumeName(base, part);
    const QFileInfo info(name);
    if (!info.isFile())
      break;
    volumes << info.absoluteFilePath();
    total += quint64(info.size());
  }
  if (volumes.isEmpty())
  {
    result.error = QObject::tr("Cannot find %1.").arg(firstVolume);
    return result;
  }
  const QString outAbs = QFileInfo(output).absoluteFilePath();
  if (volumes.contains(outAbs))
  {
    result.error = QObject::tr("The output file cannot be one of the volumes.");
    return result;
  }
  if (QFileInfo::exists(output))
  {
    result.error = QObject::tr("%1 already exists.").arg(output);
    return result;
  }
  QFile out(output);
  if (!out.open(QIODevice::WriteOnly | QIODevice::NewOnly))
  {
    result.error = QObject::tr("Cannot create %1: %2").arg(output, out.errorString());
    return result;
  }
  if (progress)
  {
    progress->setStatus(QObject::tr("Combining into %1").arg(QFileInfo(output).fileName()));
    progress->setTotals(total, quint64(volumes.size()));
  }
  quint64 done = 0;
  for (const QString &name : volumes)
  {
    QFile in(name);
    if (!in.open(QIODevice::ReadOnly))
    {
      result.error = QObject::tr("Cannot open %1: %2").arg(name, in.errorString());
      break;
    }
    if (progress)
      progress->setCurrentFile(QFileInfo(name).fileName());
    if (!streamCopy(in, out, -1, progress, &done, &result.error))
      break;
    ++result.files;
    if (progress)
      progress->setCompleted(done, result.files);
  }
  if (result.files != quint64(volumes.size()))
  {
    result.canceled = result.error.isEmpty();
    out.close();
    QFile::remove(output);
    result.files = 0;
    return result;
  }
  result.ok = true;
  return result;
}
