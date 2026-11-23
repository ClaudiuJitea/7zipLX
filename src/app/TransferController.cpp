#include "TransferController.h"

#include "backend/CompressOperation.h"
#include "backend/LocalFileOperation.h"
#include "backend/ProgressState.h"
#include "backend/PromptDispatcher.h"
#include "backend/SevenZipEngine.h"
#include "models/ArchiveFolderModel.h"
#include "ui/widgets/FilePanel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTimer>
#include <QTemporaryDir>

#include <algorithm>

namespace {

bool removeLocalPath(const QString &path)
{
  const QFileInfo info(path);
  if (info.isSymLink() || !info.isDir())
    return QFile::remove(path);
  return QDir(path).removeRecursively();
}

bool openArchiveSource(const PanelTransfer &transfer, ArchiveSession &session,
                       QVector<int> *rows, ProgressState *state,
                       PromptDispatcher *prompts, QString *error)
{
  if (!session.open(transfer.sourceRoot, QString(), state, prompts, error))
    return false;
  if (!transfer.archivePrefix.isEmpty() && !session.enterPath(transfer.archivePrefix))
  {
    *error = TransferController::tr("Cannot open folder in source archive: %1").arg(transfer.archivePrefix);
    return false;
  }
  QSet<QString> wanted;
  for (const QString &name : transfer.names)
  {
    QString clean = name;
    while (clean.endsWith(QLatin1Char('/')))
      clean.chop(1);
    if (!clean.isEmpty())
      wanted.insert(clean);
  }
  rows->clear();
  for (int row = 0; row < session.itemCount(); ++row)
  {
    QString name = session.item(row).name;
    while (name.endsWith(QLatin1Char('/')))
      name.chop(1);
    if (wanted.contains(name))
      rows->append(row);
  }
  if (rows->isEmpty())
  {
    *error = TransferController::tr("The selected items are no longer present in the source archive.");
    return false;
  }
  return true;
}

} // namespace

TransferController::TransferController(TransferContext context, QObject *parent)
    : QObject(parent), m_ctx(std::move(context))
{
}

bool TransferController::performTransfer(const PanelTransfer &transfer, FilePanel *target,
                                         const PanelDropTarget &targetPath, Qt::DropAction action,
                                         bool confirmMove)
{
  if (!target || !transfer.isValid() || !targetPath.isValid())
    return false;

  const bool move = action == Qt::MoveAction || transfer.cut;
  FilePanel *source = m_ctx.panelById(transfer.sourcePanelId);
  ArchiveSession *sourceSession = nullptr;
  QVector<int> archiveRows = transfer.archiveRows;

  if (transfer.kind == PanelTransfer::Archive)
  {
    if (source && (!source->isArchive() ||
                   source->archiveModel()->session()->archivePath() != transfer.sourceRoot))
      source = nullptr;
    if (!source)
    {
      if (m_ctx.leftPanel() && m_ctx.leftPanel()->isArchive() &&
          m_ctx.leftPanel()->archiveModel()->session()->archivePath() == transfer.sourceRoot)
        source = m_ctx.leftPanel();
      else if (m_ctx.rightPanel() && m_ctx.rightPanel()->isArchive() &&
               m_ctx.rightPanel()->archiveModel()->session()->archivePath() == transfer.sourceRoot)
        source = m_ctx.rightPanel();
    }
    if (source && source->isArchive() &&
        source->archiveModel()->session()->archivePath() == transfer.sourceRoot &&
        source->archiveRelativePath() == transfer.archivePrefix)
    {
      sourceSession = source->archiveModel()->session();
      archiveRows.clear();
      const int count = sourceSession->itemCount();
      for (int row : transfer.archiveRows)
      {
        if (row >= 0 && row < count)
          archiveRows.append(row);
      }
      if (archiveRows.isEmpty())
      {
        m_ctx.prompts->showError(TransferController::tr("Transfer"),
                             TransferController::tr("No valid source items were selected."));
        return false;
      }
    }
    // Otherwise the source panel was closed or navigated away (typical for
    // clipboard pastes).  The worker operation reopens the archive
    // temporarily when sourceSession is null.
  }
  else
  {
    if (transfer.localPaths.isEmpty())
      return false;
    for (const QString &path : transfer.localPaths)
    {
      if (QFileInfo::exists(path) || QFileInfo(path).isSymLink())
        continue;
      m_ctx.prompts->showError(TransferController::tr("Transfer"),
                           TransferController::tr("The source item no longer exists:\n%1").arg(path));
      return false;
    }
  }

  ArchiveSession *targetSession = target->isArchive() ? target->archiveModel()->session() : nullptr;
  if (targetPath.archive)
  {
    if (target->isNestedArchive())
    {
      if (move)
      {
        m_ctx.prompts->showInfo(TransferController::tr("Nested archive"),
                            TransferController::tr("Moving items into a nested archive is not supported because "
                                           "the source would be removed before the outer archive is saved.\n\n"
                                           "Copy the items instead (plain drag or Ctrl-drag)."));
        return false;
      }
      QString reason;
      if (!target->canCommitNestedArchive(&reason))
      {
        m_ctx.prompts->showError(TransferController::tr("Nested archive"), reason);
        return false;
      }
    }
    if (!targetSession)
    {
      m_ctx.prompts->showError(TransferController::tr("Transfer"),
                           TransferController::tr("No archive is open in the destination panel."));
      return false;
    }
    if (transfer.kind == PanelTransfer::Archive &&
        targetSession->archivePath() == transfer.sourceRoot)
    {
      m_ctx.prompts->showInfo(TransferController::tr("Transfer"),
                          TransferController::tr("Source and destination are the same archive."));
      return false;
    }
  }
  else
  {
    if (!QFileInfo(targetPath.localDir).isDir())
    {
      m_ctx.prompts->showError(TransferController::tr("Transfer"),
                           TransferController::tr("The destination folder does not exist:\n%1")
                               .arg(targetPath.localDir));
      return false;
    }
    const QString sourceDir = transfer.kind == PanelTransfer::Archive
        ? QString() : QFileInfo(transfer.localPaths.first()).absolutePath();
    if (!sourceDir.isEmpty() &&
        QFileInfo(sourceDir).absoluteFilePath() == QFileInfo(targetPath.localDir).absoluteFilePath())
    {
      m_ctx.prompts->showInfo(TransferController::tr("Transfer"),
                          TransferController::tr("Source and destination are the same folder."));
      return false;
    }
  }

  QString confirmation;
  if (move)
  {
    if (transfer.kind == PanelTransfer::Archive)
      confirmation = TransferController::tr("The selected archive items will be copied to the destination and then removed from the source archive. Continue?");
    else if (targetPath.archive)
      confirmation = TransferController::tr("The selected files will be added to the archive and then removed from the local filesystem. Continue?");
    else if (confirmMove)
      confirmation = TransferController::tr("Move the selected item(s)? The source is removed after a successful copy.");
  }
  if (!confirmation.isEmpty() &&
      !m_ctx.prompts->confirm(TransferController::tr("Move"), confirmation, true))
    return false;

  OperationResult result;
  if (!targetPath.archive && transfer.kind != PanelTransfer::Archive)
  {
    const QStringList paths = transfer.localPaths;
    const QString targetDir = targetPath.localDir;
    result = m_ctx.run(move ? TransferController::tr("Moving") : TransferController::tr("Copying"),
                          [paths, targetDir, move](ProgressState *state, PromptDispatcher *prompts) {
                            return copyLocalItems(paths, targetDir, move, state, prompts);
                          });
    if (result.ok)
    {
      target->refresh();
      selectTransferredItems(target, targetPath, transfer);
    }
  }
  else if (!targetPath.archive && transfer.kind == PanelTransfer::Archive)
  {
    const QString targetDir = targetPath.localDir;
    result = m_ctx.run(move ? TransferController::tr("Moving from archive") : TransferController::tr("Extracting"),
                          [transfer, sourceSession, archiveRows, targetDir, move](ProgressState *state, PromptDispatcher *prompts) {
                            ArchiveSession temporary;
                            ArchiveSession *session = sourceSession;
                            QVector<int> rows = archiveRows;
                            if (!session)
                            {
                              QString openError;
                              if (!openArchiveSource(transfer, temporary, &rows, state, prompts, &openError))
                              {
                                OperationResult failed;
                                failed.error = openError;
                                return failed;
                              }
                              session = &temporary;
                            }
                            ExtractOptions options;
                            options.outputDir = targetDir;
                            options.pathMode = 1;
                            options.overwriteMode = move ? 1 : 0;
                            options.password = session->password();
                            OperationResult extracted = session->extract(rows, options, state, prompts);
                            if (!extracted.ok || !move)
                              return extracted;
                            OperationResult removed = session->deleteItems(rows, state, prompts);
                            removed.files += extracted.files;
                            removed.folders += extracted.folders;
                            removed.unpackedSize += extracted.unpackedSize;
                            removed.packedSize += extracted.packedSize;
                            removed.messages = extracted.messages + removed.messages;
                            return removed;
                          });
    if (result.ok)
    {
      target->refresh();
      selectTransferredItems(target, targetPath, transfer);
    }
  }
  else if (targetPath.archive && transfer.kind != PanelTransfer::Archive)
  {
    const QStringList paths = transfer.localPaths;
    CompressOptions options;
    options.archivePath = targetSession->archivePath();
    options.archiveSubdir = targetPath.archivePrefix;
    options.formatIndex = SevenZipEngine::instance().formatIndex(targetSession->formatName());
    options.password = targetSession->password();
    options.level = 5;
    result = m_ctx.run(move ? TransferController::tr("Moving to archive") : TransferController::tr("Adding to archive"),
                          [paths, options](ProgressState *state, PromptDispatcher *prompts) {
                            return compressFiles(paths, options, state, prompts);
                          });
    if (result.ok && move)
    {
      QStringList failed;
      for (const QString &path : paths)
      {
        if (!removeLocalPath(path))
          failed << path;
      }
      if (!failed.isEmpty())
        m_ctx.prompts->showError(TransferController::tr("Remove source"),
                             TransferController::tr("The items were added to the archive, but these sources could not be removed:\n%1")
                                 .arg(failed.join(QLatin1Char('\n'))));
      if (source)
        source->refresh();
    }
  }
  else if (targetPath.archive && transfer.kind == PanelTransfer::Archive)
  {
    const QString targetArchive = targetSession->archivePath();
    const QString targetFormat = targetSession->formatName();
    const QString targetPassword = targetSession->password();
    const QString targetPrefix = targetPath.archivePrefix;
    result = m_ctx.run(move ? TransferController::tr("Moving archive entries")
                               : TransferController::tr("Copying archive entries"),
                          [transfer, sourceSession, archiveRows, targetArchive, targetFormat,
                           targetPassword, targetPrefix, move]
                          (ProgressState *state, PromptDispatcher *prompts) {
                            ArchiveSession temporary;
                            ArchiveSession *session = sourceSession;
                            QVector<int> rows = archiveRows;
                            if (!session)
                            {
                              QString openError;
                              if (!openArchiveSource(transfer, temporary, &rows, state, prompts, &openError))
                              {
                                OperationResult failed;
                                failed.error = openError;
                                return failed;
                              }
                              session = &temporary;
                            }
                            QTemporaryDir temp;
                            if (!temp.isValid())
                            {
                              OperationResult failed;
                              failed.error = TransferController::tr("Cannot create a temporary directory");
                              return failed;
                            }
                            ExtractOptions extractOptions;
                            extractOptions.outputDir = temp.path();
                            extractOptions.pathMode = 1;
                            extractOptions.overwriteMode = 1;
                            extractOptions.password = session->password();
                            OperationResult extracted = session->extract(rows, extractOptions, state, prompts);
                            if (!extracted.ok)
                              return extracted;
                            const QFileInfoList tops = QDir(temp.path()).entryInfoList(
                                QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
                            if (tops.isEmpty())
                            {
                              OperationResult failed;
                              failed.error = TransferController::tr("No files were extracted from the source archive");
                              return failed;
                            }
                            QStringList paths;
                            for (const QFileInfo &info : tops)
                              paths << info.absoluteFilePath();
                            CompressOptions options;
                            options.archivePath = targetArchive;
                            options.archiveSubdir = targetPrefix;
                            options.formatIndex = SevenZipEngine::instance().formatIndex(targetFormat);
                            options.password = targetPassword;
                            options.level = 5;
                            OperationResult added = compressFiles(paths, options, state, prompts);
                            added.messages = extracted.messages + added.messages;
                            added.files += extracted.files;
                            if (!added.ok || !move)
                              return added;
                            OperationResult removed = session->deleteItems(rows, state, prompts);
                            removed.files += added.files;
                            removed.messages = added.messages + removed.messages;
                            return removed;
                          });
  }

  if (!result.ok)
  {
    if (!result.canceled && !result.error.isEmpty())
      m_ctx.prompts->showError(TransferController::tr("Transfer failed"), result.error);
    return false;
  }

  bool nestedSaved = true;
  if (targetPath.archive && target->isNestedArchive())
    nestedSaved = m_ctx.commitNested(target);

  if (targetPath.archive)
  {
    const QString prefix = targetPath.archivePrefix;
    QString error;
    if (!target->reloadArchive(nullptr, m_ctx.prompts, &error))
    {
      m_ctx.prompts->showError(TransferController::tr("Reload archive"), error);
      return nestedSaved;
    }
    if (target->archiveRelativePath() != prefix && !prefix.isEmpty())
      target->enterArchivePath(prefix);
    if (target->archiveRelativePath() == prefix)
      selectTransferredItems(target, targetPath, transfer);
  }
  if (move && source)
    source->refresh();
  m_ctx.updateActions();
  if (!nestedSaved)
    m_ctx.setStatus(TransferController::tr("Nested archive changes could not be saved"));
  else
    m_ctx.setStatus(TransferController::tr("%1 %2 item(s)")
                          .arg(move ? TransferController::tr("Moved") : TransferController::tr("Copied"))
                          .arg(result.files ? result.files : transfer.count()));
  return nestedSaved;
}

void TransferController::selectTransferredItems(FilePanel *target, const PanelDropTarget &targetPath,
                                        const PanelTransfer &transfer)
{
  if (!target)
    return;
  QStringList names;
  for (const QString &name : transfer.names)
  {
    QString clean = name;
    while (clean.endsWith(QLatin1Char('/')))
      clean.chop(1);
    if (!clean.isEmpty())
      names << clean;
  }
  if (names.isEmpty())
    return;
  if (targetPath.archive)
  {
    if (target->archiveRelativePath() != targetPath.archivePrefix)
      return;
    QTimer::singleShot(0, target, [target, names] { target->selectArchiveEntries(names); });
  }
  else
  {
    QStringList paths;
    for (const QString &name : names)
      paths << targetPath.localDir + QLatin1Char('/') + name;
    QTimer::singleShot(0, target, [target, paths] { target->selectLocalPaths(paths); });
  }
}
