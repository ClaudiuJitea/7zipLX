#include "LocalFsModel.h"

#include <QDir>
#include <QDirIterator>
#include <QLocale>
#include <QTimer>
#include <QCollator>
#include <QFile>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <QMimeData>
#include <QUrl>

namespace {

struct ScanRequest
{
  QString path;
  bool showHidden = false;
  bool showDots = true;
  bool flatMode = false;
  int sortColumn = 0;
  Qt::SortOrder sortOrder = Qt::AscendingOrder;
  quint64 generation = 0;
};

LocalFsScanResult scanLocalFolder(const ScanRequest &request)
{
  LocalFsScanResult result;
  result.path = request.path;
  result.generation = request.generation;
  QDir dir(request.path);
  if (!dir.exists())
    return result;

  QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
  if (request.showHidden)
    filters |= QDir::Hidden;
  if (request.flatMode)
  {
    QDirIterator it(request.path, filters, QDirIterator::Subdirectories);
    while (it.hasNext())
      result.entries.append(it.nextFileInfo());
  }
  else
  {
    result.entries = dir.entryInfoList(filters, QDir::NoSort);
    if (request.showDots && request.path != QLatin1String("/"))
    {
      QFileInfo parent(request.path + QLatin1String("/.."));
      if (parent.exists())
        result.entries.prepend(parent);
    }
  }

  QCollator collator;
  collator.setNumericMode(true);
  collator.setCaseSensitivity(Qt::CaseInsensitive);
  const int column = request.sortColumn;
  const Qt::SortOrder order = request.sortOrder;
  std::stable_sort(result.entries.begin(), result.entries.end(),
                   [&](const QFileInfo &a, const QFileInfo &b) {
    const bool ad = a.isDir();
    const bool bd = b.isDir();
    if (ad != bd)
      return ad;
    int cmp = 0;
    switch (column)
    {
      case 1: cmp = a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1); break;
      case 2: cmp = collator.compare(a.suffix(), b.suffix()); break;
      case 3: cmp = a.lastModified() == b.lastModified() ? 0 : (a.lastModified() < b.lastModified() ? -1 : 1); break;
      case 4: cmp = int(a.permissions()) - int(b.permissions()); break;
      default:
        cmp = collator.compare(request.flatMode ? dir.relativeFilePath(a.absoluteFilePath()) : a.fileName(),
                               request.flatMode ? dir.relativeFilePath(b.absoluteFilePath()) : b.fileName());
        break;
    }
    if (cmp == 0)
      cmp = collator.compare(a.fileName(), b.fileName());
    return order == Qt::AscendingOrder ? cmp < 0 : cmp > 0;
  });
  result.ok = true;
  return result;
}

} // namespace

LocalFsModel::LocalFsModel(QObject *parent) : QAbstractTableModel(parent)
{
  m_refreshTimer.setSingleShot(true);
  m_refreshTimer.setInterval(250);
  connect(&m_refreshTimer, &QTimer::timeout, this, &LocalFsModel::refresh);
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &LocalFsModel::scheduleRefresh);
  connect(&m_scanWatcher, &QFutureWatcher<LocalFsScanResult>::finished, this, [this] {
    const LocalFsScanResult result = m_scanWatcher.result();
    if (result.generation != m_generation || result.path != m_path)
      return;
    m_batchGeneration = result.generation;
    beginResetModel();
    m_entries.clear();
    endResetModel();
    m_pendingEntries = result.entries;
    m_pendingIndex = 0;
    if (m_pendingEntries.isEmpty())
    {
      finishLoading();
      return;
    }
    applyNextBatch();
  });
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
  m_refreshTimer.stop();
  ++m_generation;
  ScanRequest request;
  request.path = m_path;
  request.showHidden = m_showHidden;
  request.showDots = m_showDots;
  request.flatMode = m_flatMode;
  request.sortColumn = m_sortColumn;
  request.sortOrder = m_sortOrder;
  request.generation = m_generation;

  if (!m_loading)
  {
    m_loading = true;
    emit loadingChanged(true);
  }
  beginResetModel();
  m_entries.clear();
  m_pendingEntries.clear();
  m_pendingIndex = 0;
  endResetModel();

  // QFutureWatcher tracks only the latest request; older scans finish and are
  // ignored through the generation check above.
  m_scanWatcher.setFuture(QtConcurrent::run(scanLocalFolder, request));
}

void LocalFsModel::applyNextBatch()
{
  if (m_batchGeneration != m_generation)
    return;
  if (m_pendingIndex >= m_pendingEntries.size())
  {
    finishLoading();
    return;
  }
  const int count = qMin(1000, m_pendingEntries.size() - m_pendingIndex);
  const int first = m_entries.size();
  beginInsertRows(QModelIndex(), first, first + count - 1);
  for (int i = 0; i < count; ++i)
    m_entries.append(m_pendingEntries.at(m_pendingIndex + i));
  endInsertRows();
  m_pendingIndex += count;
  QTimer::singleShot(0, this, &LocalFsModel::applyNextBatch);
}

void LocalFsModel::finishLoading()
{
  m_pendingEntries.clear();
  m_pendingIndex = 0;
  if (!m_loading)
    return;
  m_loading = false;
  emit loadingChanged(false);
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
        if (fi.isDir()) return LocalFsModel::tr("Folder");
        if (fi.isSymLink()) return LocalFsModel::tr("Link -> %1").arg(fi.symLinkTarget());
        return fi.suffix().isEmpty() ? LocalFsModel::tr("File") : LocalFsModel::tr("%1 file").arg(fi.suffix().toUpper());
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
    case ColumnName: return LocalFsModel::tr("Name");
    case ColumnSize: return LocalFsModel::tr("Size");
    case ColumnType: return LocalFsModel::tr("Type");
    case ColumnModified: return LocalFsModel::tr("Modified");
    case ColumnPermissions: return LocalFsModel::tr("Permissions");
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
