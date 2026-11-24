#include "ArchiveFolderModel.h"

#include <QLocale>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QMimeDatabase>

ArchiveFolderModel::ArchiveFolderModel(QObject *parent) : QAbstractTableModel(parent) {}

bool ArchiveFolderModel::open(const QString &path, const QString &formatHint,
                              ProgressState *progress, PromptDispatcher *prompts,
                              QString *error, const QString &password)
{
  if (!m_session)
    m_session = std::make_unique<ArchiveSession>();
  if (!m_session->open(path, formatHint, progress, prompts, error, password))
  {
    m_session.reset();
    return false;
  }
  refresh();
  emit archiveOpened(path);
  return true;
}

void ArchiveFolderModel::close()
{
  if (m_session)
    m_session->close();
  m_session.reset();
  beginResetModel();
  m_entries.clear();
  endResetModel();
}

bool ArchiveFolderModel::isOpen() const { return m_session && m_session->isOpen(); }

bool ArchiveFolderModel::setFlatMode(bool flat)
{
  if (!m_session)
    return false;
  if (!m_session->setFlatMode(flat))
    return false;
  refresh();
  emit pathChanged(path());
  return true;
}

void ArchiveFolderModel::refresh()
{
  beginResetModel();
  m_entries.clear();
  if (m_session && m_session->isOpen())
  {
    const int n = m_session->itemCount();
    m_entries.reserve(n);
    for (int i = 0; i < n; ++i)
      m_entries.append(m_session->item(i));
  }
  endResetModel();
}

bool ArchiveFolderModel::enter(int item)
{
  if (!m_session || item < 0 || item >= m_entries.size())
    return false;
  if (!m_session->enter(item))
    return false;
  refresh();
  emit pathChanged(path());
  return true;
}

bool ArchiveFolderModel::up()
{
  if (!m_session || !m_session->up())
    return false;
  refresh();
  emit pathChanged(path());
  return true;
}

bool ArchiveFolderModel::root()
{
  if (!m_session || !m_session->root())
    return false;
  refresh();
  emit pathChanged(path());
  return true;
}

QString ArchiveFolderModel::path() const { return m_session ? m_session->currentPath() : QString(); }
QStringList ArchiveFolderModel::breadcrumbs() const { return m_session ? m_session->breadcrumbs() : QStringList(); }

const ArchiveEntryInfo &ArchiveFolderModel::entry(int item) const
{
  static const ArchiveEntryInfo empty;
  if (item < 0 || item >= m_entries.size())
    return empty;
  return m_entries.at(item);
}

bool ArchiveFolderModel::hasParentRow() const { return m_showParentRow && m_session && m_session->isOpen(); }

void ArchiveFolderModel::setShowParentRow(bool show)
{
  if (m_showParentRow == show)
    return;
  beginResetModel();
  m_showParentRow = show;
  endResetModel();
}

bool ArchiveFolderModel::isParentRow(int row) const { return row == 0 && hasParentRow(); }

int ArchiveFolderModel::itemForRow(int row) const
{
  const int item = hasParentRow() ? row - 1 : row;
  return (item >= 0 && item < m_entries.size()) ? item : -1;
}

int ArchiveFolderModel::rowForItem(int item) const
{
  if (item < 0 || item >= m_entries.size())
    return -1;
  return hasParentRow() ? item + 1 : item;
}

QVector<int> ArchiveFolderModel::allRows() const
{
  QVector<int> rows;
  rows.reserve(m_entries.size());
  for (int i = 0; i < m_entries.size(); ++i)
    rows.append(i);
  return rows;
}

int ArchiveFolderModel::rowCount(const QModelIndex &parent) const
{
  if (parent.isValid())
    return 0;
  return m_entries.size() + (hasParentRow() ? 1 : 0);
}
int ArchiveFolderModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : ColumnCount; }

QIcon ArchiveFolderModel::iconForName(const QString &name) const
{
  // Same look as the local file list: the theme icon for the file type.
  const QString suffix = QFileInfo(name).suffix().toLower();
  const QString key = QStringLiteral("ext:") + suffix;
  auto it = m_icons.find(key);
  if (it != m_icons.end())
    return it.value();
  static const QMimeDatabase mimes;
  const QMimeType mime = mimes.mimeTypeForFile(name, QMimeDatabase::MatchExtension);
  QIcon result = QIcon::fromTheme(mime.iconName());
  if (result.isNull())
    result = QIcon::fromTheme(mime.genericIconName());
  if (result.isNull())
    result = icon(false);
  m_icons.insert(key, result);
  return result;
}

QIcon ArchiveFolderModel::icon(bool dir) const
{
  const QString key = dir ? QStringLiteral("dir") : QStringLiteral("file");
  auto it = m_icons.find(key);
  if (it != m_icons.end())
    return it.value();
  static const QFileIconProvider provider;
  const QIcon result = dir
      ? QIcon::fromTheme(QStringLiteral("folder"), provider.icon(QFileIconProvider::Folder))
      : QIcon::fromTheme(QStringLiteral("text-x-generic"), provider.icon(QFileIconProvider::File));
  m_icons.insert(key, result);
  return result;
}

QVariant ArchiveFolderModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid())
    return QVariant();
  if (isParentRow(index.row()))
  {
    switch (role)
    {
      case Qt::DisplayRole:
        if (index.column() == ColumnName) return QStringLiteral("..");
        if (index.column() == ColumnSize) return ArchiveFolderModel::tr("<DIR>");
        return QVariant();
      case Qt::DecorationRole:
        if (index.column() == ColumnName)
          return icon(true);
        return QVariant();
      case Qt::TextAlignmentRole:
        if (index.column() == ColumnSize) return int(Qt::AlignRight | Qt::AlignVCenter);
        return QVariant();
      case Qt::ToolTipRole: return ArchiveFolderModel::tr("Up one level");
      case Qt::UserRole: return QStringLiteral("..");
      case Qt::UserRole + 1: return true;
      default: return QVariant();
    }
  }
  const int item = itemForRow(index.row());
  if (item < 0)
    return QVariant();
  const ArchiveEntryInfo &e = m_entries.at(item);
  if (role == Qt::DisplayRole)
  {
    switch (index.column())
    {
      case ColumnName: return e.name;
      case ColumnSize: return e.isDir ? ArchiveFolderModel::tr("<DIR>") : QLocale().formattedDataSize(qint64(e.size), 1, QLocale::DataSizeTraditionalFormat);
      case ColumnPacked: return e.packedSize ? QLocale().formattedDataSize(qint64(e.packedSize), 1, QLocale::DataSizeTraditionalFormat) : QString();
      case ColumnModified: return e.modified.isValid() ? QLocale().toString(e.modified, QLocale::ShortFormat) : QString();
      case ColumnAttributes: return QStringLiteral("0x%1").arg(e.attrib, 8, 16, QLatin1Char('0'));
      case ColumnCrc: return e.hasCrc ? ArchiveFolderModel::tr("%1").arg(e.crc, 8, 16, QLatin1Char('0')) : QString();
      case ColumnMethod: return e.method;
      case ColumnEncrypted: return e.isEncrypted ? ArchiveFolderModel::tr("Yes") : QString();
    }
  }
  else if (role == Qt::DecorationRole && index.column() == ColumnName)
  {
    return e.isDir ? icon(true) : iconForName(e.name);
  }
  else if (role == Qt::TextAlignmentRole && (index.column() == ColumnSize || index.column() == ColumnPacked || index.column() == ColumnModified))
  {
    return int(Qt::AlignRight | Qt::AlignVCenter);
  }
  else if (role == Qt::ToolTipRole)
  {
    return e.path.isEmpty() ? e.name : e.path;
  }
  else if (role == Qt::UserRole)
  {
    return e.name;
  }
  else if (role == Qt::UserRole + 1)
  {
    return e.isDir;
  }
  return QVariant();
}

QVariant ArchiveFolderModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
  switch (section)
  {
    case ColumnName: return ArchiveFolderModel::tr("Name");
    case ColumnSize: return ArchiveFolderModel::tr("Size");
    case ColumnPacked: return ArchiveFolderModel::tr("Packed");
    case ColumnModified: return ArchiveFolderModel::tr("Modified");
    case ColumnAttributes: return ArchiveFolderModel::tr("Attributes");
    case ColumnCrc: return ArchiveFolderModel::tr("CRC");
    case ColumnMethod: return ArchiveFolderModel::tr("Method");
    case ColumnEncrypted: return ArchiveFolderModel::tr("Encrypted");
    default: return QVariant();
  }
}

Qt::ItemFlags ArchiveFolderModel::flags(const QModelIndex &index) const
{
  if (!index.isValid()) return Qt::NoItemFlags;
  if (isParentRow(index.row()))
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
}
