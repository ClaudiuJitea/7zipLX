#include "RootModel.h"

#include <QDir>
#include <QLocale>
#include <QStandardPaths>

RootModel::RootModel(QObject *parent) : QAbstractTableModel(parent) { refresh(); }

void RootModel::refresh()
{
  beginResetModel();
  m_items.clear();
  Item home;
  home.name = QStringLiteral("Home");
  home.path = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  home.fs = QStringLiteral("home");
  home.root = false;
  m_items.append(home);

  const auto volumes = QStorageInfo::mountedVolumes();
  for (const QStorageInfo &v : volumes)
  {
    if (!v.isValid() || !v.isReady())
      continue;
    Item it;
    it.name = v.displayName().isEmpty() ? v.rootPath() : v.displayName();
    it.path = v.rootPath();
    it.fs = QString::fromUtf8(v.fileSystemType());
    it.total = quint64(v.bytesTotal());
    it.free = quint64(v.bytesAvailable());
    it.root = v.rootPath() == QLatin1String("/");
    m_items.append(it);
  }
  endResetModel();
}

QString RootModel::pathForRow(int row) const
{
  if (row < 0 || row >= m_items.size()) return QString();
  return m_items.at(row).path;
}

int RootModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_items.size(); }
int RootModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : ColumnCount; }

QVariant RootModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
    return QVariant();
  const Item &it = m_items.at(index.row());
  if (role == Qt::DisplayRole)
  {
    switch (index.column())
    {
      case ColumnName: return it.name;
      case ColumnMount: return it.path;
      case ColumnFs: return it.fs;
      case ColumnTotal: return it.total ? QLocale().formattedDataSize(qint64(it.total), 1, QLocale::DataSizeTraditionalFormat) : QString();
      case ColumnFree: return it.free ? QLocale().formattedDataSize(qint64(it.free), 1, QLocale::DataSizeTraditionalFormat) : QString();
    }
  }
  else if (role == Qt::DecorationRole && index.column() == ColumnName)
  {
    return QIcon::fromTheme(it.root ? QStringLiteral("drive-harddisk") : QStringLiteral("folder"));
  }
  else if (role == Qt::UserRole)
  {
    return it.path;
  }
  else if (role == Qt::TextAlignmentRole && (index.column() == ColumnTotal || index.column() == ColumnFree))
  {
    return int(Qt::AlignRight | Qt::AlignVCenter);
  }
  return QVariant();
}

QVariant RootModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
  switch (section)
  {
    case ColumnName: return QStringLiteral("Name");
    case ColumnMount: return QStringLiteral("Mount point");
    case ColumnFs: return QStringLiteral("Filesystem");
    case ColumnTotal: return QStringLiteral("Total");
    case ColumnFree: return QStringLiteral("Free");
    default: return QVariant();
  }
}
