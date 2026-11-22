#pragma once

#include <QWidget>
#include <QVector>
#include <QStringList>
#include <QSet>
#include <memory>

#include "backend/ArchiveSession.h"
#include "PanelTransfer.h"

class LocalFsModel;
class ArchiveFolderModel;
class RootModel;
class ProgressState;
class PromptDispatcher;
class QComboBox;
class QTableView;
class QLabel;
class QToolButton;
class QTemporaryDir;
class FileView;

class FilePanel : public QWidget
{
  Q_OBJECT
public:
  enum Mode { LocalMode, ArchiveMode, RootMode };

  explicit FilePanel(QWidget *parent = nullptr);

  void setLocalPath(const QString &path);
  void setRootMode();
  void setShowHidden(bool show);
  void setShowDots(bool show);
  void setShowGrid(bool show);
  void setFlatMode(bool flat);
  bool openArchive(const QString &path, ProgressState *progress, PromptDispatcher *prompts,
                   QString *error, const QString &password = QString(), const QString &formatHint = QString());
  bool openNestedArchive(int row, ProgressState *progress, PromptDispatcher *prompts, QString *error,
                         const QString &formatHint = QString());
  void closeArchive();
  void refresh();
  bool goUp();
  void focusView();
  void focusPathBar();
  bool enterArchivePath(const QString &relativePath);

  void setPanelId(int id);
  int panelId() const { return m_panelId; }
  void setActive(bool active);
  bool isActive() const { return m_active; }
  void setDropHighlight(bool active);

  void selectAllRows();
  void selectLocalPaths(const QStringList &paths);
  void selectArchiveEntries(const QStringList &names);
  PanelTransfer selectionTransfer() const;
  PanelDropTarget dropTargetForPosition(const QPoint &viewportPos) const;
  QString archiveRelativePath() const;

  Mode mode() const { return m_mode; }
  bool isArchive() const;
  bool isNestedArchive() const { return !m_archiveParents.isEmpty(); }
  bool reloadArchive(ProgressState *progress, PromptDispatcher *prompts, QString *error);
  bool canCommitNestedArchive(QString *reason) const;
  OperationResult commitNestedArchive(ProgressState *progress, PromptDispatcher *prompts);
  bool refreshNestedParents(ProgressState *progress, PromptDispatcher *prompts, QString *error);
  QString localPath() const;
  QString displayPath() const;

  QStringList selectedLocalPaths() const;
  QVector<int> selectedArchiveRows() const;
  int currentRow() const;
  void invertSelection();

  LocalFsModel *localModel() const { return m_localModel; }
  ArchiveFolderModel *archiveModel() const { return m_archiveModel; }
  QTableView *view() const { return m_view; }

signals:
  void pathChanged(const QString &path);
  void activeRequested();
  void openArchiveRequested(const QString &path);
  void statusMessage(const QString &message);
  void filesMovedExternally(const QStringList &paths);
  void nestedArchiveRequested(int row);
  void contextMenuRequested(const QPoint &globalPos);
  void transferRequested(const PanelTransfer &transfer, const PanelDropTarget &target, Qt::DropAction action);
  void focusOtherPanelRequested();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
  void onActivated(const QModelIndex &index);
  void onPathEntered();
  void onSelectionChanged();
  void onCustomContextMenu(const QPoint &pos);

private:
  void setMode(Mode mode);
  void clearNestedArchives();
  void updateStatus();
  void updatePathBar();
  void connectSelectionModel();
  void connectModelSignals();
  QString localKeyForRow(int row) const;
  QString archiveKeyForRow(int row) const;
  void saveLocalSelection();
  void saveArchiveSelection();
  void scheduleLocalSelectionRestore();
  void scheduleArchiveSelectionRestore();
  void restoreLocalSelection();
  void restoreArchiveSelection();

  Mode m_mode = LocalMode;
  int m_panelId = -1;
  bool m_active = false;
  bool m_flatMode = false;
  QString m_lastLocalPath;
  LocalFsModel *m_localModel = nullptr;
  ArchiveFolderModel *m_archiveModel = nullptr;
  QVector<ArchiveFolderModel *> m_archiveParents;
  QStringList m_archiveParentRoots;
  QVector<std::shared_ptr<QTemporaryDir>> m_archiveTemps;
  QString m_archiveRootDisplay;
  RootModel *m_rootModel = nullptr;
  QComboBox *m_pathBar = nullptr;
  QToolButton *m_upButton = nullptr;
  QToolButton *m_refreshButton = nullptr;
  QTableView *m_view = nullptr;
  QLabel *m_status = nullptr;

  QSet<QString> m_savedLocalSelection;
  QString m_savedLocalCurrent;
  QString m_savedLocalRoot;
  QSet<QString> m_savedArchiveSelection;
  QString m_savedArchiveCurrent;
  QString m_savedArchivePrefix;
  QString m_savedArchiveRoot;
  bool m_restoringSelection = false;
};
