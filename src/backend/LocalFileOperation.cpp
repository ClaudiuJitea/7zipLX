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
