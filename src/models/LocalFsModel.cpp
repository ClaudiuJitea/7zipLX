#include "LocalFsModel.h"

#include <QDir>
#include <QDirIterator>
#include <QLocale>
#include <QTimer>
#include <QCollator>
#include <QFile>
#include <algorithm>
#include <QMimeData>
#include <QUrl>

LocalFsModel::LocalFsModel(QObject *parent) : QAbstractTableModel(parent)
{
  m_refreshTimer.setSingleShot(true);
  m_refreshTimer.setInterval(250);
  connect(&m_refreshTimer, &QTimer::timeout, this, &LocalFsModel::refresh);
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &LocalFsModel::scheduleRefresh);
}

void LocalFsModel::setPath(const QString &path)
{
  const QFileInfo fi(path);
  QString abs = fi.absoluteFilePath();
  if (abs.isEmpty())
    abs = QDir::currentPath();
  if (!QFileInfo(abs).isDir())
    abs = QFileInfo(abs).absolutePath();
  if (m_path == abs)
  {
    refresh();
    return;
  }
  if (!m_path.isEmpty())
    m_watcher.removePath(m_path);
  m_path = abs;
  if (QFileInfo(m_path).isDir())
    m_watcher.addPath(m_path);
  refresh();
  emit pathChanged(m_path);
}

void LocalFsModel::setShowHidden(bool show)
{
  if (m_showHidden == show) return;
  m_showHidden = show;
  refresh();
}

void LocalFsModel::setShowDots(bool show)
{
  if (m_showDots == show) return;
  m_showDots = show;
  refresh();
}

void LocalFsModel::setFlatMode(bool flat)
{
  if (m_flatMode == flat) return;
  m_flatMode = flat;
  refresh();
}

void LocalFsModel::scheduleRefresh()
{
  m_refreshTimer.start();
}

void LocalFsModel::refresh()
{
  QFileInfoList list;
  QDir dir(m_path);
  if (dir.exists())
  {
    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
    if (m_showHidden)
      filters |= QDir::Hidden;
    if (m_flatMode)
    {
      QDirIterator it(m_path, filters, QDirIterator::Subdirectories);
      while (it.hasNext())
      {
        const QFileInfo fi = it.nextFileInfo();
        list.append(fi);
      }
    }
    else
    {
      list = dir.entryInfoList(filters, QDir::NoSort);
      if (m_showDots && m_path != QLatin1String("/"))
      {
        QFileInfo parent(m_path + QLatin1String("/.."));
        if (parent.exists())
        {
          list.prepend(parent);
        }
      }
    }
  }

  QCollator collator;
  collator.setNumericMode(true);
  collator.setCaseSensitivity(Qt::CaseInsensitive);
  const int col = m_sortColumn;
  const Qt::SortOrder order = m_sortOrder;
  std::stable_sort(list.begin(), list.end(), [&](const QFileInfo &a, const QFileInfo &b) {
    const bool ad = a.isDir();
    const bool bd = b.isDir();
    if (ad != bd)
      return ad;
    int cmp = 0;
    switch (col)
    {
      case ColumnSize: cmp = a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1); break;
      case ColumnType: cmp = collator.compare(a.suffix(), b.suffix()); break;
      case ColumnModified: cmp = a.lastModified() == b.lastModified() ? 0 : (a.lastModified() < b.lastModified() ? -1 : 1); break;
      case ColumnPermissions: cmp = int(a.permissions()) - int(b.permissions()); break;
      case ColumnName:
      default:
        cmp = collator.compare(m_flatMode ? dir.relativeFilePath(a.absoluteFilePath()) : a.fileName(),
                               m_flatMode ? dir.relativeFilePath(b.absoluteFilePath()) : b.fileName());
        break;
    }
    if (cmp == 0)
      cmp = collator.compare(a.fileName(), b.fileName());
    return order == Qt::AscendingOrder ? cmp < 0 : cmp > 0;
  });

  beginResetModel();
  m_entries = list;
  endResetModel();
}

QFileInfo LocalFsModel::fileInfo(int row) const
{
  if (row < 0 || row >= m_entries.size())
    return QFileInfo();
  return m_entries.at(row);
}

bool LocalFsModel::isParentRow(int row) const
{
  if (m_flatMode) return false;
  const QFileInfo fi = fileInfo(row);
  return fi.fileName() == QLatin1String("..") && fi.isDir();
}

QString LocalFsModel::filePath(int row) const
{
  const QFileInfo fi = fileInfo(row);
  if (isParentRow(row))
    return fi.absoluteFilePath();
  return fi.absoluteFilePath();
}

QIcon LocalFsModel::iconFor(const QFileInfo &fi) const
{
  if (fi.isDir())
    return QIcon::fromTheme(QStringLiteral("folder"), m_iconProvider.icon(QFileIconProvider::Folder));
  if (fi.isSymLink())
    return QIcon::fromTheme(QStringLiteral("emblem-symbolic-link"), m_iconProvider.icon(fi));
  const QString key = fi.suffix().toLower();
  auto it = m_iconCache.find(key);
  if (it != m_iconCache.end())
    return it.value();
  QIcon icon = m_iconProvider.icon(fi);
  m_iconCache.insert(key, icon);
  return icon;
}

int LocalFsModel::rowCount(const QModelIndex &parent) const
{
  if (parent.isValid()) return 0;
  return m_entries.size();
}

int LocalFsModel::columnCount(const QModelIndex &parent) const
{
  if (parent.isValid()) return 0;
  return ColumnCount;
}

QVariant LocalFsModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
    return QVariant();
  const QFileInfo &fi = m_entries.at(index.row());
  const bool parentRow = fi.fileName() == QLatin1String("..") && fi.isDir();

  if (role == Qt::DisplayRole)
  {
    switch (index.column())
    {
      case ColumnName:
        if (parentRow) return QStringLiteral("..");
        if (m_flatMode) return QDir(m_path).relativeFilePath(fi.absoluteFilePath());
        return fi.fileName();
      case ColumnSize:
        if (fi.isDir()) return QStringLiteral("<DIR>");
        return QLocale().formattedDataSize(fi.size(), 1, QLocale::DataSizeTraditionalFormat);
      case ColumnType:
        if (fi.isDir()) return QStringLiteral("Folder");
        if (fi.isSymLink()) return QStringLiteral("Link -> %1").arg(fi.symLinkTarget());
        return fi.suffix().isEmpty() ? QStringLiteral("File") : QStringLiteral("%1 file").arg(fi.suffix().toUpper());
      case ColumnModified:
        return QLocale().toString(fi.lastModified(), QLocale::ShortFormat);
      case ColumnPermissions:
      {
        const QFile::Permissions p = fi.permissions();
        QString s;
        s += (p & QFileDevice::ReadOwner) ? QLatin1Char('r') : QLatin1Char('-');
        s += (p & QFileDevice::WriteOwner) ? QLatin1Char('w') : QLatin1Char('-');
        s += (p & QFileDevice::ExeOwner) ? QLatin1Char('x') : QLatin1Char('-');
        s += QLatin1Char('/');
        s += (p & QFileDevice::ReadGroup) ? QLatin1Char('r') : QLatin1Char('-');
        s += (p & QFileDevice::WriteGroup) ? QLatin1Char('w') : QLatin1Char('-');
        s += (p & QFileDevice::ExeGroup) ? QLatin1Char('x') : QLatin1Char('-');
        s += QLatin1Char('/');
        s += (p & QFileDevice::ReadOther) ? QLatin1Char('r') : QLatin1Char('-');
        s += (p & QFileDevice::WriteOther) ? QLatin1Char('w') : QLatin1Char('-');
        s += (p & QFileDevice::ExeOther) ? QLatin1Char('x') : QLatin1Char('-');
        return s;
      }
    }
  }
  else if (role == Qt::DecorationRole && index.column() == ColumnName)
  {
    return iconFor(fi);
  }
  else if (role == Qt::TextAlignmentRole && (index.column() == ColumnSize || index.column() == ColumnModified))
  {
    return int(Qt::AlignRight | Qt::AlignVCenter);
  }
  else if (role == Qt::ToolTipRole)
  {
    return fi.absoluteFilePath();
  }
  else if (role == Qt::UserRole)
  {
    return fi.absoluteFilePath();
  }
  else if (role == Qt::UserRole + 1)
  {
    return fi.isDir();
  }
  else if (role == Qt::UserRole + 2)
  {
    return parentRow;
  }
  return QVariant();
}

QVariant LocalFsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    return QVariant();
  switch (section)
  {
    case ColumnName: return QStringLiteral("Name");
    case ColumnSize: return QStringLiteral("Size");
    case ColumnType: return QStringLiteral("Type");
    case ColumnModified: return QStringLiteral("Modified");
    case ColumnPermissions: return QStringLiteral("Permissions");
    default: return QVariant();
  }
}

Qt::ItemFlags LocalFsModel::flags(const QModelIndex &index) const
{
  if (!index.isValid()) return Qt::NoItemFlags;
  Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  if (!isParentRow(index.row()))
    f |= Qt::ItemIsDragEnabled;
  return f;
}

QStringList LocalFsModel::mimeTypes() const
{
  return {QStringLiteral("text/uri-list")};
}

QMimeData *LocalFsModel::mimeData(const QModelIndexList &indexes) const
{
  auto *mime = new QMimeData;
  QList<QUrl> urls;
  QSet<int> rows;
  for (const QModelIndex &idx : indexes)
    if (idx.isValid())
      rows.insert(idx.row());
  for (int row : rows)
  {
    if (isParentRow(row)) continue;
    const QFileInfo fi = fileInfo(row);
    if (fi.exists())
      urls << QUrl::fromLocalFile(fi.absoluteFilePath());
  }
  mime->setUrls(urls);
  return mime;
}

Qt::DropActions LocalFsModel::supportedDragActions() const
{
  return Qt::CopyAction | Qt::MoveAction;
}

void LocalFsModel::sort(int column, Qt::SortOrder order)
{
  if (column < 0 || column >= ColumnCount)
    return;
  m_sortColumn = column;
  m_sortOrder = order;
  refresh();
}
