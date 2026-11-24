#pragma once

#include <QWidget>
#include <QVector>
#include <functional>
#include <QStringList>
#include <QSet>
#include <memory>

#include "backend/ArchiveSession.h"
#include "backend/ArchiveWriteBack.h"
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
  void updateThemeIcons();
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

  // Shows only rows whose name contains |text| (case-insensitive; * and ?
  // work as wildcards).  The ".." row always stays.  Cleared when the folder
  // changes.
  void setNameFilter(const QString &text);
  QString nameFilter() const { return m_nameFilter; }
  int visibleItemCount() const;
  QString emptyMessage() const;

  void selectAllRows();
  void selectLocalPaths(const QStringList &paths);
  void selectArchiveEntries(const QStringList &names);
  PanelTransfer selectionTransfer() const;
  using DragMaterializer = std::function<QStringList(FilePanel *, const PanelTransfer &, QString *)>;
  void setDragMaterializer(DragMaterializer materializer);
  using DragReleaser = std::function<void(FilePanel *, const QStringList &)>;
  void setDragReleaser(DragReleaser releaser);
  QStringList materializeForDrag(const PanelTransfer &transfer, QString *error) const;
  void releaseDrag(const QStringList &paths) const;
  PanelDropTarget dropTargetForPosition(const QPoint &viewportPos) const;
  QString archiveRelativePath() const;
  // The archive file on disk that contains what is shown: the outermost one
  // when a nested archive is open (nested ones live in temporary folders).
  QString rootArchivePath() const;

  Mode mode() const { return m_mode; }
  bool isArchive() const;
  bool isNestedArchive() const { return !m_archiveParents.isEmpty(); }
  bool reloadArchive(ProgressState *progress, PromptDispatcher *prompts, QString *error);
  bool canCommitNestedArchive(QString *reason) const;
  OperationResult commitNestedArchive(ProgressState *progress, PromptDispatcher *prompts);
  QVector<ArchiveWriteBackStep> archiveWriteBackSteps(const QString &entryPath) const;
  bool refreshNestedParents(ProgressState *progress, PromptDispatcher *prompts, QString *error);
  QString localPath() const;
  QString displayPath() const;

  QStringList selectedLocalPaths() const;
  // Session item indices (not view rows) of the selected archive entries.
  QVector<int> selectedArchiveRows() const;
  int currentRow() const;
  // Session item index of the current row in archive mode, -1 for ".." or none.
  int currentArchiveItem() const;
  bool isParentRow(int row) const;
  // Selects the given local paths once the current folder finishes loading.
  void selectLocalPathsWhenLoaded(const QStringList &paths);
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
  void nameFilterChanged(const QString &text);

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
  void connectArchiveModelSignals(ArchiveFolderModel *model);
  QString localKeyForRow(int row) const;
  QString archiveKeyForItem(int item) const;
  void saveLocalSelection();
  void saveArchiveSelection();
  void scheduleLocalSelectionRestore();
  void scheduleArchiveSelectionRestore();
  void restoreLocalSelection();
  void restoreArchiveSelection();
  void applyNameFilter();
  bool rowMatchesFilter(int row) const;

  Mode m_mode = LocalMode;
  int m_panelId = -1;
  bool m_active = false;
  bool m_flatMode = false;
  bool m_showDots = true;
  QString m_nameFilter;
  QString m_filterLocation;
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
  DragMaterializer m_dragMaterializer;
  DragReleaser m_dragReleaser;

  QSet<QString> m_savedLocalSelection;
  QString m_savedLocalCurrent;
  QString m_savedLocalRoot;
  QSet<QString> m_savedArchiveSelection;
  QString m_savedArchiveCurrent;
  QString m_savedArchivePrefix;
  QString m_savedArchiveRoot;
  bool m_restoringSelection = false;
};
