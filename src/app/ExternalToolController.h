#pragma once

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <memory>

#include "backend/ArchiveWriteBack.h"

class FilePanel;
class OperationRunner;
class PromptDispatcher;
class QTemporaryDir;
struct PanelTransfer;

// Owns temporary copies used by View/Edit/Diff/drag-out and writes edits made
// in an external editor back into the source archive.  Temp copies are capped
// and pruned; an edited copy is never removed silently while it has unsaved
// changes.
class ExternalToolController : public QObject
{
  Q_OBJECT
public:
  ExternalToolController(QWidget *parent, OperationRunner *runner, PromptDispatcher *prompts);
  ~ExternalToolController() override;

  bool view(FilePanel *panel, QString *error);
  bool edit(FilePanel *panel, QString *error);
  bool openWithDesktop(FilePanel *panel, QString *error);
  void diff(FilePanel *panel);
  QStringList materializeForDrag(FilePanel *panel, const PanelTransfer &transfer, QString *error);
  void releaseDrag(const QStringList &paths);

  int pendingCount() const;
  bool hasPendingEdits() const;
  void saveAllPending();
  void discardPending();

signals:
  void pendingEditsChanged(int count);
  void statusMessage(const QString &text);

private:
  struct Session
  {
    quint64 id = 0;
    QPointer<FilePanel> panel;
    QString displayName;
    QString archiveLabel;
    QString filePath;
    std::shared_ptr<QTemporaryDir> temp;
    QVector<ArchiveWriteBackStep> steps;
    bool editable = false;
    bool pending = false;
    bool pinned = false;
    quint64 sizeHint = 0;
    QDateTime baselineModified;
    qint64 baselineSize = 0;
    QDateTime promptedModified;
    qint64 promptedSize = 0;
    QDateTime lastUsed;
    QDateTime createdAt;
  };

  bool materializeSingle(FilePanel *panel, bool editable, QString *path, QString *error);
  QStringList materializeRows(FilePanel *panel, const QVector<int> &rows, bool editable,
                              bool allowReuse, QString *error);
  Session *findById(quint64 id) const;
  void addSession(const Session &session);
  void removeSession(quint64 id);
  void pruneTemps(quint64 keepId = 0);
  void pollEditedFiles();
  void expireTemps();
  void checkEditedFile(quint64 id);
  void promptWriteBack(quint64 id);
  void scheduleNextPrompt();
  void writeBack(quint64 id);
  bool launchExternal(const QString &command, const QString &path, QString *error);
  QString itemFullPath(FilePanel *panel, int row) const;

  QWidget *m_parent = nullptr;
  OperationRunner *m_runner = nullptr;
  PromptDispatcher *m_prompts = nullptr;
  QFileSystemWatcher m_watcher;
  QTimer m_pollTimer;
  QTimer m_cleanupTimer;
  QVector<Session> m_sessions;
  QSet<quint64> m_prompting;
  QVector<quint64> m_promptQueue;
  bool m_promptScheduled = false;
  quint64 m_nextId = 1;
};
