#pragma once

#include <QAbstractTableModel>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QTimer>
#include <QHash>
#include <QIcon>
#include <QMimeData>

struct LocalFsScanResult
{
  QString path;
  quint64 generation = 0;
  QFileInfoList entries;
  bool ok = false;
};

class LocalFsModel : public QAbstractTableModel
{
  Q_OBJECT
public:
  enum Column { ColumnName = 0, ColumnSize, ColumnType, ColumnModified, ColumnPermissions, ColumnCount };

  explicit LocalFsModel(QObject *parent = nullptr);

  void setPath(const QString &path);
  QString path() const { return m_path; }

  void setShowHidden(bool show);
  bool showHidden() const { return m_showHidden; }
  void setShowDots(bool show);
  void setFlatMode(bool flat);
  bool flatMode() const { return m_flatMode; }
  bool showDots() const { return m_showDots; }

  void refresh();
  bool isLoading() const { return m_loading; }
  QFileInfo fileInfo(int row) const;
  bool isParentRow(int row) const;
  QString filePath(int row) const;

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;
  void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
  QStringList mimeTypes() const override;
  QMimeData *mimeData(const QModelIndexList &indexes) const override;
  Qt::DropActions supportedDragActions() const override;

signals:
  void pathChanged(const QString &path);
  void loadingChanged(bool loading);

private:
  QIcon iconFor(const QFileInfo &fi) const;
  void scheduleRefresh();
  void applyNextBatch();
  void finishLoading();

  QString m_path;
  QFileInfoList m_entries;
  bool m_showHidden = false;
  bool m_showDots = true;
  bool m_flatMode = false;
  int m_sortColumn = ColumnName;
  Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
  mutable QFileIconProvider m_iconProvider;
  mutable QHash<QString, QIcon> m_iconCache;
  QFileSystemWatcher m_watcher;
  QTimer m_refreshTimer;
  QFutureWatcher<LocalFsScanResult> m_scanWatcher;
  QFileInfoList m_pendingEntries;
  int m_pendingIndex = 0;
  quint64 m_generation = 0;
  quint64 m_batchGeneration = 0;
  bool m_loading = false;
};
