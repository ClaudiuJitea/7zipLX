#pragma once

#include <QAbstractTableModel>
#include <QStorageInfo>
#include <QVector>
#include <QIcon>

class RootModel : public QAbstractTableModel
{
  Q_OBJECT
public:
  enum Column { ColumnName = 0, ColumnMount, ColumnFs, ColumnTotal, ColumnFree, ColumnCount };
  explicit RootModel(QObject *parent = nullptr);
  void refresh();
  QString pathForRow(int row) const;
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
  struct Item { QString name; QString path; QString fs; quint64 total = 0; quint64 free = 0; bool root = false; };
  QVector<Item> m_items;
};
