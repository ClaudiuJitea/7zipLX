#pragma once

#include <QWidget>
#include <QVector>
#include <QStringList>
#include <memory>

#include "backend/ArchiveSession.h"

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

  Mode mode() const { return m_mode; }
  bool isArchive() const;
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
  void filesDropped(const QStringList &paths, bool move);
  void nestedArchiveRequested(int row);
  void contextMenuRequested(const QPoint &globalPos);

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

  Mode m_mode = LocalMode;
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
};
