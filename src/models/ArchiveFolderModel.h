#pragma once

#include <QAbstractTableModel>
#include <QVector>
#include <QHash>
#include <memory>
#include <QIcon>

#include "backend/ArchiveSession.h"

class ProgressState;
class PromptDispatcher;

class ArchiveFolderModel : public QAbstractTableModel
{
  Q_OBJECT
public:
  enum Column { ColumnName = 0, ColumnSize, ColumnPacked, ColumnModified, ColumnAttributes, ColumnCrc, ColumnMethod, ColumnEncrypted, ColumnCount };

  explicit ArchiveFolderModel(QObject *parent = nullptr);

  bool open(const QString &path, const QString &formatHint,
            ProgressState *progress, PromptDispatcher *prompts,
            QString *error, const QString &password = QString());
  void close();
  bool isOpen() const;
  ArchiveSession *session() const { return m_session.get(); }

  // Item indices address the session's entries in the current folder.  The
  // view shows a synthetic ".." row first whenever an archive is open, so view
  // rows and item indices differ by one; use itemForRow()/rowForItem().
  bool enter(int item);
  bool up();
  bool root();
  void refresh();
  bool setFlatMode(bool flat);

  QString path() const;
  QStringList breadcrumbs() const;
  const ArchiveEntryInfo &entry(int item) const;
  QVector<int> allRows() const;
  int itemCount() const { return m_entries.size(); }
  bool hasParentRow() const;
  void setShowParentRow(bool show);
  bool isParentRow(int row) const;
  int itemForRow(int row) const;
  int rowForItem(int item) const;

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;

signals:
  void pathChanged(const QString &path);
  void archiveOpened(const QString &path);

private:
  QIcon icon(bool dir) const;
  QIcon iconForName(const QString &name) const;

  std::unique_ptr<ArchiveSession> m_session;
  QVector<ArchiveEntryInfo> m_entries;
  bool m_showParentRow = true;
  mutable QHash<QString, QIcon> m_icons;
};
