#include "ExternalToolController.h"

#include "OperationRunner.h"
#include "backend/ArchiveSession.h"
#include "backend/MessageDialog.h"
#include "backend/PromptDispatcher.h"
#include "backend/ProgressState.h"
#include "ui/dialogs/HashResultDialog.h"
#include "ui/widgets/FilePanel.h"
#include "ui/widgets/PanelTransfer.h"
#include "models/ArchiveFolderModel.h"
#include "models/LocalFsModel.h"

#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QUrl>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>

namespace {

QString commandSetting(const char *key, const char *fallback)
{
  QSettings settings;
  const QString value =
      settings.value(QString::fromLatin1(key), QString::fromLatin1(fallback)).toString().trimmed();
  return value.isEmpty() ? QString::fromLatin1(fallback) : value;
}

QString firstFileIn(const QString &dir)
{
  QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
  if (it.hasNext())
    return it.next();
  return QString();
}

} // namespace

ExternalToolController::ExternalToolController(QWidget *parent, OperationRunner *runner,
                                               PromptDispatcher *prompts)
    : QObject(parent), m_parent(parent), m_runner(runner), m_prompts(prompts)
{
  connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
    for (const Session &session : m_sessions)
    {
      if (session.editable && session.filePath == path)
      {
        checkEditedFile(session.id);
        if (!m_watcher.files().contains(path) && QFileInfo::exists(path))
          m_watcher.addPath(path);
        break;
      }
    }
  });

  // QFileSystemWatcher is not reliable for atomic editor saves (the inode is
  // replaced), so poll the small set of editable copies as a safety net.
  m_pollTimer.setInterval(1500);
  connect(&m_pollTimer, &QTimer::timeout, this, &ExternalToolController::pollEditedFiles);
  m_pollTimer.start();

  m_cleanupTimer.setInterval(60 * 1000);
  connect(&m_cleanupTimer, &QTimer::timeout, this, &ExternalToolController::expireTemps);
  m_cleanupTimer.start();
}

ExternalToolController::~ExternalToolController()
{
  for (const QString &path : m_watcher.files())
    m_watcher.removePath(path);
}

bool ExternalToolController::view(FilePanel *panel, QString *error)
{
  QString path;
  if (!materializeSingle(panel, false, &path, error))
    return false;
  return launchExternal(commandSetting("tools/viewerCommand", "xdg-open"), path, error);
}

bool ExternalToolController::edit(FilePanel *panel, QString *error)
{
  QString path;
  if (!materializeSingle(panel, true, &path, error))
    return false;
  return launchExternal(commandSetting("tools/editorCommand", "xdg-open"), path, error);
}

bool ExternalToolController::openWithDesktop(FilePanel *panel, QString *error)
{
  QString path;
  if (!materializeSingle(panel, false, &path, error))
    return false;
  if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
  {
    if (error)
      *error = tr("Cannot open %1.").arg(path);
    return false;
  }
  return true;
}

void ExternalToolController::diff(FilePanel *panel)
{
  if (!panel)
    return;

  QStringList paths;
  QStringList errors;
  if (!panel->isArchive())
  {
    paths = panel->selectedLocalPaths();
    if (paths.size() != 2)
    {
      m_prompts->showInfo(tr("Diff"), tr("Select exactly two files to compare."));
      return;
    }
  }
  else
  {
    const QVector<int> rows = panel->selectedArchiveRows();
    if (rows.size() != 2)
    {
      m_prompts->showInfo(tr("Diff"), tr("Select exactly two archive items to compare."));
      return;
    }
    for (int row : rows)
    {
      QString error;
      const QStringList materialized = materializeRows(panel, {row}, false, false, &error);
      if (materialized.isEmpty())
      {
        if (!error.isEmpty())
          errors << error;
      }
      else
      {
        paths << materialized.first();
      }
    }
    if (paths.size() != 2)
    {
      if (!errors.isEmpty())
        m_prompts->showError(tr("Diff"), errors.join(QLatin1Char('\n')));
      return;
    }
  }

  QStringList parts = QProcess::splitCommand(commandSetting("tools/diffCommand", "diff -u"));
  if (parts.isEmpty())
  {
    m_prompts->showError(tr("Diff"), tr("The diff command is empty."));
    return;
  }
  const QString program = parts.takeFirst();
  auto *process = new QProcess(this);
  process->setProgram(program);
  process->setArguments(parts + paths);

  auto finishWithOutput = [this, process](const QString &output) {
    process->deleteLater();
    HashResultDialog dialog(tr("Diff"), output.isEmpty()
        ? QStringList{tr("No differences.")}
        : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts), m_parent);
    dialog.exec();
  };

  connect(process, &QProcess::errorOccurred, this, [this, process, program](QProcess::ProcessError err) {
    if (err == QProcess::FailedToStart)
    {
      process->deleteLater();
      m_prompts->showError(tr("Diff"), tr("Cannot start %1.").arg(program));
    }
  });
  connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [process, finishWithOutput](int exitCode, QProcess::ExitStatus status) {
            QString output = QString::fromLocal8Bit(process->readAllStandardOutput()) +
                             QString::fromLocal8Bit(process->readAllStandardError());
            if (status == QProcess::NormalExit && exitCode > 1 && output.isEmpty())
              output = ExternalToolController::tr("The diff command exited with code %1.").arg(exitCode);
            finishWithOutput(output);
          });

  // Never block the GUI thread on an external program.  A runaway diff is
  // killed after a generous timeout instead of freezing the window.
  QTimer::singleShot(60000, process, [process] {
    if (process->state() != QProcess::NotRunning)
      process->kill();
  });

  emit statusMessage(tr("Comparing..."));
  process->start();
}

QStringList ExternalToolController::materializeForDrag(FilePanel *panel,
                                                       const PanelTransfer &transfer,
                                                       QString *error)
{
  if (!panel)
    return {};
  if (transfer.kind == PanelTransfer::Local)
    return transfer.localPaths;
  if (!panel->isArchive() || transfer.archiveRows.isEmpty() ||
      !panel->archiveModel() || !panel->archiveModel()->session())
  {
    if (error)
      *error = tr("No archive items were selected.");
    return {};
  }

  ArchiveSession *session = panel->archiveModel()->session();
  auto temp = std::make_shared<QTemporaryDir>();
  if (!temp->isValid())
  {
    if (error)
      *error = tr("Cannot create a temporary directory.");
    return {};
  }

  quint64 sizeHint = 0;
  for (int row : transfer.archiveRows)
  {
    if (row >= 0 && row < session->itemCount())
      sizeHint += session->item(row).size;
  }

  const QString outputDir = temp->path();
  const QString password = session->password();
  const QVector<int> rows = transfer.archiveRows;
  const OperationResult extracted = m_runner->run(
      tr("Preparing drag"),
      [session, rows, outputDir, password](ProgressState *state, PromptDispatcher *prompts) {
        ExtractOptions options;
        options.outputDir = outputDir;
        options.pathMode = 1;
        options.overwriteMode = 1;
        options.password = password;
        return session->extract(rows, options, state, prompts);
      });
  if (!extracted.ok)
  {
    if (error && !extracted.canceled)
      *error = extracted.error;
    return {};
  }

  QStringList paths;
  const QFileInfoList tops = QDir(outputDir).entryInfoList(
      QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
  for (const QFileInfo &info : tops)
    paths << info.absoluteFilePath();
  if (paths.isEmpty())
  {
    if (error)
      *error = tr("The item could not be extracted.");
    return {};
  }

  Session drag;
  drag.id = m_nextId++;
  drag.panel = panel;
  drag.archiveLabel = panel->displayPath();
  drag.filePath = outputDir;
  drag.temp = temp;
  drag.editable = false;
  drag.pinned = true;
  drag.sizeHint = sizeHint;
  drag.lastUsed = QDateTime::currentDateTime();
  drag.createdAt = drag.lastUsed;
  addSession(drag);
  return paths;
}

void ExternalToolController::releaseDrag(const QStringList &paths)
{
  for (Session &session : m_sessions)
  {
    if (session.filePath.isEmpty())
      continue;
    for (const QString &path : paths)
    {
      if (path == session.filePath ||
          path.startsWith(session.filePath + QLatin1Char('/')))
      {
        session.pinned = false;
        break;
      }
    }
  }
  pruneTemps();
}

int ExternalToolController::pendingCount() const
{
  int count = 0;
  for (const Session &session : m_sessions)
    if (session.editable && session.pending)
      ++count;
  return count;
}

bool ExternalToolController::hasPendingEdits() const
{
  return pendingCount() > 0;
}

void ExternalToolController::saveAllPending()
{
  QVector<quint64> ids;
  for (const Session &session : m_sessions)
    if (session.editable && session.pending)
      ids << session.id;
  for (quint64 id : ids)
    writeBack(id);
}

void ExternalToolController::discardPending()
{
  QVector<quint64> ids;
  for (const Session &session : m_sessions)
    if (session.editable)
      ids << session.id;
  for (quint64 id : ids)
    removeSession(id);
}

bool ExternalToolController::materializeSingle(FilePanel *panel, bool editable, QString *path,
                                               QString *error)
{
  if (error)
    error->clear();
  if (!panel)
    return false;

  if (!panel->isArchive())
  {
    QStringList selected = panel->selectedLocalPaths();
    if (selected.isEmpty())
    {
      const int row = panel->currentRow();
      if (row >= 0 && panel->localModel())
        selected << panel->localModel()->filePath(row);
    }
    if (selected.size() != 1)
    {
      if (error)
        *error = tr("Select one file first.");
      return false;
    }
    const QFileInfo info(selected.first());
    if (info.isDir())
    {
      if (error)
        *error = tr("Folders cannot be opened in an external application.");
      return false;
    }
    if (path)
      *path = info.absoluteFilePath();
    return true;
  }

  const int row = panel->currentRow();
  if (row < 0)
  {
    if (error)
      *error = tr("Select one item first.");
    return false;
  }
  const QStringList files = materializeRows(panel, {row}, editable, true, error);
  if (files.isEmpty())
    return false;
  if (path)
    *path = files.first();
  return true;
}

QStringList ExternalToolController::materializeRows(FilePanel *panel, const QVector<int> &rows,
                                                    bool editable, bool allowReuse,
                                                    QString *error)
{
  QStringList result;
  if (!panel || !panel->isArchive() || !panel->archiveModel() ||
      !panel->archiveModel()->session())
  {
    if (error)
      *error = tr("No archive is open.");
    return result;
  }

  ArchiveSession *session = panel->archiveModel()->session();
  const QString password = session->password();

  // Make room before adding the new copies, never after: a batch (for example
  // a multi-file drag) must stay alive as long as the caller needs the paths.
  pruneTemps();

  struct Pending
  {
    int row = -1;
    QString fullPath;
    QString label;
    quint64 sourceSize = 0;
    QVector<ArchiveWriteBackStep> steps;
    std::shared_ptr<QTemporaryDir> temp;
    QString filePath;
    quint64 reusedId = 0;
  };
  QVector<Pending> pending;

  for (int row : rows)
  {
    if (row < 0 || row >= session->itemCount())
      continue;
    const ArchiveEntryInfo entry = session->item(row);
    if (entry.isDir)
      continue;

    Pending item;
    item.row = row;
    item.fullPath = itemFullPath(panel, row);
    item.label = panel->displayPath() + QLatin1Char('/') + entry.name;
    item.sourceSize = entry.size;
    item.steps = panel->archiveWriteBackSteps(item.fullPath);

    if (allowReuse)
    {
      for (const Session &existing : m_sessions)
      {
        if (existing.editable != editable || !existing.panel || existing.panel != panel)
          continue;
        if (existing.archiveLabel != item.label || existing.steps != item.steps)
          continue;
        if (!QFileInfo::exists(existing.filePath))
          continue;
        item.reusedId = existing.id;
        item.filePath = existing.filePath;
        break;
      }
    }
    pending.append(item);
  }

  if (pending.isEmpty())
  {
    if (error)
      *error = tr("No files were selected.");
    return result;
  }

  struct ExtractJob
  {
    int row = -1;
    QString outputDir;
  };
  QVector<ExtractJob> jobs;
  for (Pending &item : pending)
  {
    if (item.reusedId != 0)
      continue;
    item.temp = std::make_shared<QTemporaryDir>();
    if (!item.temp->isValid())
    {
      if (error)
        *error = tr("Cannot create a temporary directory.");
      return {};
    }
    jobs.append({item.row, item.temp->path()});
  }

  if (!jobs.isEmpty())
  {
    const OperationResult extracted = m_runner->run(
        editable ? tr("Preparing editor") : tr("Preparing viewer"),
        [session, password, jobs](ProgressState *state, PromptDispatcher *prompts) {
          OperationResult total;
          total.ok = true;
          for (const ExtractJob &job : jobs)
          {
            ExtractOptions options;
            options.outputDir = job.outputDir;
            options.pathMode = 2;
            options.overwriteMode = 1;
            options.password = password;
            const OperationResult one =
                session->extract({job.row}, options, state, prompts);
            if (!one.ok)
              return one;
            total.files += one.files;
            total.unpackedSize += one.unpackedSize;
          }
          return total;
        });
    if (!extracted.ok)
    {
      if (error && !extracted.canceled)
        *error = extracted.error;
      return {};
    }
  }

  for (Pending &item : pending)
  {
    if (item.reusedId != 0)
    {
      for (Session &existing : m_sessions)
      {
        if (existing.id == item.reusedId)
        {
          existing.lastUsed = QDateTime::currentDateTime();
          existing.panel = panel;
          result << existing.filePath;
          break;
        }
      }
      continue;
    }

    const QString file = firstFileIn(item.temp->path());
    if (file.isEmpty())
    {
      if (error)
        *error = tr("The item could not be extracted.");
      return {};
    }

    Session sessionItem;
    sessionItem.id = m_nextId++;
    sessionItem.panel = panel;
    sessionItem.displayName = item.label;
    sessionItem.archiveLabel = item.label;
    sessionItem.filePath = file;
    sessionItem.temp = item.temp;
    sessionItem.steps = item.steps;
    sessionItem.editable = editable;
    sessionItem.sizeHint = qMax<quint64>(quint64(QFileInfo(file).size()), item.sourceSize);
    sessionItem.lastUsed = QDateTime::currentDateTime();
    sessionItem.createdAt = sessionItem.lastUsed;
    const QFileInfo info(file);
    sessionItem.baselineModified = info.lastModified();
    sessionItem.baselineSize = info.size();
    sessionItem.promptedModified = sessionItem.baselineModified;
    sessionItem.promptedSize = sessionItem.baselineSize;
    addSession(sessionItem);
    result << file;
  }

  // Give external viewers time to open the copy before the size cap is
  // enforced, then keep the cache bounded even if no further action happens.
  if (!editable)
    QTimer::singleShot(60000, this, [this] { pruneTemps(); });
  return result;
}

ExternalToolController::Session *ExternalToolController::findById(quint64 id) const
{
  for (int i = 0; i < m_sessions.size(); ++i)
    if (m_sessions.at(i).id == id)
      return const_cast<Session *>(&m_sessions.at(i));
  return nullptr;
}

void ExternalToolController::addSession(const Session &session)
{
  m_sessions.append(session);
  if (session.editable && !session.filePath.isEmpty() && QFileInfo::exists(session.filePath))
    m_watcher.addPath(session.filePath);
}

void ExternalToolController::removeSession(quint64 id)
{
  bool wasPending = false;
  for (int i = 0; i < m_sessions.size(); ++i)
  {
    if (m_sessions.at(i).id != id)
      continue;
    wasPending = m_sessions.at(i).pending;
    const QString path = m_sessions.at(i).filePath;
    m_sessions.removeAt(i);
    bool stillUsed = false;
    for (const Session &other : m_sessions)
      if (other.filePath == path)
        stillUsed = true;
    if (!path.isEmpty() && !stillUsed)
      m_watcher.removePath(path);
    break;
  }
  if (wasPending)
    emit pendingEditsChanged(pendingCount());
}

void ExternalToolController::pruneTemps(quint64 keepId)
{
  QSettings settings;
  const qint64 limitBytes =
      qint64(settings.value(QStringLiteral("tools/externalTempLimitMiB"), 512).toInt()) * 1024 * 1024;
  if (limitBytes <= 0)
    return;

  auto total = [this] {
    qint64 sum = 0;
    for (const Session &session : m_sessions)
    {
      const QFileInfo info(session.filePath);
      sum += info.isFile() ? info.size() : qint64(session.sizeHint);
    }
    return sum;
  };

  if (total() <= limitBytes)
    return;

  // Never silently discard a copy that an editor may still be using.  The cap
  // controls view/drag copies; edit copies live until write-back, discard or
  // application exit, where the user is asked explicitly.
  QVector<quint64> removable;
  for (const Session &session : m_sessions)
    if (session.id != keepId && !session.pinned && !session.editable)
      removable << session.id;

  std::sort(removable.begin(), removable.end(), [this](quint64 a, quint64 b) {
    const Session *sa = findById(a);
    const Session *sb = findById(b);
    if (!sa || !sb)
      return a < b;
    return sa->lastUsed < sb->lastUsed;
  });

  for (quint64 id : removable)
  {
    if (total() <= limitBytes)
      break;
    removeSession(id);
  }
}

void ExternalToolController::expireTemps()
{
  QSettings settings;
  const int maxAgeMinutes =
      qMax(1, settings.value(QStringLiteral("tools/externalTempMaxAgeMinutes"), 30).toInt());
  const QDateTime now = QDateTime::currentDateTime();
  QVector<quint64> expired;
  for (const Session &session : m_sessions)
  {
    if (session.editable || session.pinned)
      continue;
    if (session.lastUsed.secsTo(now) >= maxAgeMinutes * 60)
      expired << session.id;
  }
  for (quint64 id : expired)
    removeSession(id);
  pruneTemps();
}

void ExternalToolController::pollEditedFiles()
{
  QVector<quint64> ids;
  for (const Session &session : m_sessions)
  {
    if (!session.editable)
      continue;
    if (!m_watcher.files().contains(session.filePath) && QFileInfo::exists(session.filePath))
      m_watcher.addPath(session.filePath);
    ids << session.id;
  }
  for (quint64 id : ids)
    checkEditedFile(id);
}

void ExternalToolController::checkEditedFile(quint64 id)
{
  Session *session = findById(id);
  if (!session || !session->editable || m_prompting.contains(id))
    return;
  const QFileInfo info(session->filePath);
  if (!info.exists())
    return;
  const QDateTime modified = info.lastModified();
  const qint64 size = info.size();
  if (modified == session->promptedModified && size == session->promptedSize)
    return;
  if (modified == session->baselineModified && size == session->baselineSize)
    return;

  session->pending = true;
  session->promptedModified = modified;
  session->promptedSize = size;
  emit pendingEditsChanged(pendingCount());
  if (!m_promptQueue.contains(id))
    m_promptQueue.append(id);
  scheduleNextPrompt();
}

void ExternalToolController::scheduleNextPrompt()
{
  if (m_promptScheduled || !m_prompting.isEmpty())
    return;
  while (!m_promptQueue.isEmpty())
  {
    const quint64 id = m_promptQueue.takeFirst();
    Session *session = findById(id);
    if (session && session->editable && session->pending)
    {
      m_promptScheduled = true;
      QTimer::singleShot(250, this, [this, id] {
        m_promptScheduled = false;
        promptWriteBack(id);
      });
      return;
    }
  }
}

void ExternalToolController::promptWriteBack(quint64 id)
{
  Session *session = findById(id);
  if (!session || !session->editable || !session->pending ||
      !m_prompting.isEmpty())
  {
    scheduleNextPrompt();
    return;
  }

  m_prompting.insert(id);
  MessageDialog dialog(m_parent);
  dialog.setWindowTitle(tr("Update archive?"));
  dialog.setKind(MessageDialog::Kind::Question);
  dialog.setHeading(tr("Update archive?"));
  dialog.setText(tr("<b>%1</b> was modified in an external editor.<br><br>"
                    "Update <b>%2</b> with the new contents?")
                     .arg(QFileInfo(session->filePath).fileName().toHtmlEscaped(),
                          session->archiveLabel.toHtmlEscaped()),
                  true);
  auto *update = dialog.addButton(tr("Update Archive"), QDialogButtonBox::AcceptRole);
  auto *keep = dialog.addButton(tr("Keep Changes"), QDialogButtonBox::ActionRole);
  auto *discard = dialog.addButton(tr("Discard"), QDialogButtonBox::RejectRole);
  dialog.setDefaultButton(update);
  dialog.exec();
  m_prompting.remove(id);

  if (dialog.clickedButton() == update)
  {
    writeBack(id);
  }
  else if (dialog.clickedButton() == discard)
  {
    removeSession(id);
  }
  else if (dialog.clickedButton() == keep)
  {
    emit statusMessage(tr("The edited file was kept. Use File > Update Archive to save it later."));
  }
  scheduleNextPrompt();
}

void ExternalToolController::writeBack(quint64 id)
{
  m_promptQueue.removeAll(id);
  Session *session = findById(id);
  if (!session)
    return;
  const QVector<ArchiveWriteBackStep> steps = session->steps;
  const QString editedPath = session->filePath;
  const QPointer<FilePanel> panel = session->panel;
  const QString label = session->archiveLabel;
  if (steps.isEmpty())
  {
    m_prompts->showError(tr("Update archive"), tr("The archive is no longer available."));
    return;
  }

  const OperationResult result = m_runner->run(
      tr("Updating archive"),
      [steps, editedPath](ProgressState *state, PromptDispatcher *prompts) {
        OperationResult total;
        total.ok = true;
        QString replacement = editedPath;
        for (const ArchiveWriteBackStep &step : steps)
        {
          const OperationResult one =
              replaceArchiveEntry(step.archivePath, step.subdir, replacement, step.formatName,
                                  step.password, state, prompts);
          if (!one.ok)
            return one;
          total.files += one.files;
          total.folders += one.folders;
          total.messages += one.messages;
          replacement = step.archivePath;
        }
        return total;
      });

  if (!result.ok)
  {
    if (!result.canceled && !result.error.isEmpty())
      m_prompts->showError(tr("Update archive failed"), result.error);
    return;
  }

  Session *updated = findById(id);
  if (updated)
  {
    const QFileInfo info(updated->filePath);
    updated->baselineModified = info.lastModified();
    updated->baselineSize = info.size();
    updated->promptedModified = updated->baselineModified;
    updated->promptedSize = updated->baselineSize;
    updated->pending = false;
    updated->lastUsed = QDateTime::currentDateTime();
  }
  emit pendingEditsChanged(pendingCount());

  if (panel && panel->isArchive())
  {
    QString error;
    if (!panel->reloadArchive(nullptr, m_prompts, &error) && !error.isEmpty())
      m_prompts->showError(tr("Reload archive"), error);
    else if (panel->isNestedArchive())
      panel->refreshNestedParents(nullptr, m_prompts, &error);
  }
  emit statusMessage(tr("Updated %1").arg(label));
}

bool ExternalToolController::launchExternal(const QString &command, const QString &path,
                                            QString *error)
{
  QStringList parts = QProcess::splitCommand(command);
  if (parts.isEmpty())
  {
    if (error)
      *error = tr("The configured command is empty.");
    return false;
  }
  const QString program = parts.takeFirst();
  parts << path;
  if (!QProcess::startDetached(program, parts))
  {
    if (error)
      *error = tr("Cannot start %1.").arg(program);
    return false;
  }
  return true;
}

QString ExternalToolController::itemFullPath(FilePanel *panel, int row) const
{
  if (!panel || !panel->archiveModel() || !panel->archiveModel()->session())
    return {};
  ArchiveSession *session = panel->archiveModel()->session();
  const ArchiveEntryInfo entry = session->item(row);
  QString path = entry.path;
  if (path.isEmpty())
    path = entry.name;
  const QString prefix = session->relativePath();
  if (!prefix.isEmpty() && path != prefix && !path.startsWith(prefix + QLatin1Char('/')))
    path = prefix + QLatin1Char('/') + path;
  return path;
}
