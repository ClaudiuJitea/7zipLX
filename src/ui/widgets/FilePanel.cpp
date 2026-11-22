#include "FilePanel.h"
#include "FileView.h"
#include "PanelTransfer.h"
#include "models/LocalFsModel.h"
#include "models/ArchiveFolderModel.h"
#include "models/RootModel.h"
#include "backend/ProgressState.h"
#include "backend/PromptDispatcher.h"

#include <QComboBox>
#include <QLineEdit>
#include <QToolButton>
#include <QStyle>
#include <QHeaderView>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QDesktopServices>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QEvent>
#include <QContextMenuEvent>
#include <QLocale>
#include <QTimer>
#include <QDirIterator>
#include <QTemporaryDir>
#include <algorithm>

FilePanel::FilePanel(QWidget *parent) : QWidget(parent)
{
  setObjectName(QStringLiteral("filePanel"));
  setAttribute(Qt::WA_StyledBackground, true);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto *top = new QHBoxLayout();
  top->setContentsMargins(6, 6, 6, 4);
  top->setSpacing(5);
  m_upButton = new QToolButton(this);
  m_upButton->setAutoRaise(true);
  m_upButton->setToolTip(QStringLiteral("Up one level"));
  m_upButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/up.svg")));
  m_upButton->setIconSize(QSize(16, 16));
  top->addWidget(m_upButton);
  m_pathBar = new QComboBox(this);
  m_pathBar->setObjectName(QStringLiteral("panelPathBar"));
  m_pathBar->setEditable(true);
  m_pathBar->setInsertPolicy(QComboBox::NoInsert);
  m_pathBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_pathBar->setMinimumHeight(32);
  top->addWidget(m_pathBar, 1);
  m_refreshButton = new QToolButton(this);
  m_refreshButton->setAutoRaise(true);
  m_refreshButton->setToolTip(QStringLiteral("Refresh"));
  m_refreshButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/refresh.svg")));
  m_refreshButton->setIconSize(QSize(16, 16));
  top->addWidget(m_refreshButton);
  layout->addLayout(top);

  m_view = new FileView(this, this);
  m_view->setObjectName(QStringLiteral("fileView"));
  m_view->setAlternatingRowColors(true);
  m_view->setSortingEnabled(true);
  m_view->setShowGrid(false);
  m_view->setFrameShape(QFrame::NoFrame);
  m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_view->setWordWrap(false);
  m_view->setIconSize(QSize(18, 18));
  m_view->horizontalHeader()->setStretchLastSection(false);
  m_view->horizontalHeader()->setHighlightSections(false);
  m_view->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  m_view->verticalHeader()->setVisible(false);
  m_view->verticalHeader()->setDefaultSectionSize(29);
  m_view->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
  m_view->installEventFilter(this);
  m_view->viewport()->installEventFilter(this);
  m_pathBar->installEventFilter(this);
  m_pathBar->lineEdit()->installEventFilter(this);
  layout->addWidget(m_view, 1);

  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("panelStatus"));
  m_status->setContentsMargins(4, 1, 4, 1);
  layout->addWidget(m_status);

  m_localModel = new LocalFsModel(this);
  m_archiveModel = new ArchiveFolderModel(this);
  m_rootModel = new RootModel(this);

  setMode(LocalMode);
  connect(m_pathBar->lineEdit(), &QLineEdit::returnPressed, this, &FilePanel::onPathEntered);
  connect(m_upButton, &QToolButton::clicked, this, [this] { goUp(); });
  connect(m_refreshButton, &QToolButton::clicked, this, &FilePanel::refresh);
  connect(m_view, &QTableView::activated, this, &FilePanel::onActivated);
  connect(m_view, &QTableView::clicked, this, &FilePanel::activeRequested);
  connect(m_localModel, &LocalFsModel::pathChanged, this, [this](const QString &p) {
    m_lastLocalPath = p;
    if (m_mode == LocalMode)
    {
      updatePathBar();
      emit pathChanged(p);
    }
  });
  connect(m_archiveModel, &ArchiveFolderModel::pathChanged, this, [this](const QString &) {
    if (m_mode == ArchiveMode)
    {
      updatePathBar();
      emit pathChanged(displayPath());
      updateStatus();
    }
  });
  connectModelSignals();

  setLocalPath(QDir::homePath());
}

void FilePanel::setMode(Mode mode)
{
  m_mode = mode;
  switch (mode)
  {
    case LocalMode:
      m_view->setModel(m_localModel);
      m_pathBar->setEditable(true);
      break;
    case ArchiveMode:
      m_view->setModel(m_archiveModel);
      m_pathBar->setEditable(false);
      break;
    case RootMode:
      m_view->setModel(m_rootModel);
      m_pathBar->setEditable(false);
      break;
  }
  auto *header = m_view->horizontalHeader();
  header->setSectionResizeMode(QHeaderView::Interactive);
  header->setSectionResizeMode(0, QHeaderView::Stretch);
  for (int i = 1; i < m_view->model()->columnCount(); ++i)
    header->setSectionResizeMode(i, QHeaderView::ResizeToContents);
  if (mode == LocalMode)
  {
    m_view->setColumnHidden(2, false);
    m_view->setColumnHidden(4, true); // permissions are in Properties
  }
  else if (mode == ArchiveMode)
  {
    for (int i = 4; i < m_view->model()->columnCount(); ++i)
      m_view->setColumnHidden(i, true);
  }
  connectSelectionModel();
  m_view->sortByColumn(0, Qt::AscendingOrder);
  updatePathBar();
  updateStatus();
}

void FilePanel::setLocalPath(const QString &path)
{
  closeArchive();
  m_lastLocalPath = path;
  setMode(LocalMode);
  m_localModel->setPath(path);
  updatePathBar();
  emit pathChanged(m_localModel->path());
  updateStatus();
}

void FilePanel::setShowHidden(bool show)
{
  m_localModel->setShowHidden(show);
}

void FilePanel::setShowDots(bool show)
{
  m_localModel->setShowDots(show);
}

void FilePanel::setShowGrid(bool show)
{
  m_view->setShowGrid(show);
}

void FilePanel::setFlatMode(bool flat)
{
  m_flatMode = flat;
  m_localModel->setFlatMode(flat);
  if (m_mode == ArchiveMode && m_archiveModel && m_archiveModel->isOpen())
  {
    if (m_archiveModel->setFlatMode(flat))
    {
      updatePathBar();
      emit pathChanged(displayPath());
      updateStatus();
    }
  }
  else if (m_mode == LocalMode)
  {
    updateStatus();
  }
}

void FilePanel::setRootMode()
{
  closeArchive();
  setMode(RootMode);
  m_rootModel->refresh();
  updatePathBar();
  emit pathChanged(QStringLiteral("Computer"));
  updateStatus();
}

bool FilePanel::openArchive(const QString &path, ProgressState *progress, PromptDispatcher *prompts,
                            QString *error, const QString &password, const QString &formatHint)
{
  clearNestedArchives();
  m_archiveRootDisplay.clear();
  m_savedArchiveSelection.clear();
  m_savedArchivePrefix.clear();
  m_savedArchiveRoot.clear();
  QString err;
  if (!m_archiveModel->open(path, formatHint, progress, prompts, &err, password))
  {
    if (error) *error = err;
    if (m_mode == ArchiveMode)
    {
      setMode(LocalMode);
      m_localModel->setPath(m_lastLocalPath.isEmpty() ? QDir::homePath() : m_lastLocalPath);
      updatePathBar();
    }
    return false;
  }
  m_archiveRootDisplay = m_archiveModel->path();
  setMode(ArchiveMode);
  if (m_flatMode)
    setFlatMode(true);
  updatePathBar();
  emit pathChanged(displayPath());
  updateStatus();
  return true;
}

void FilePanel::clearNestedArchives()
{
  for (ArchiveFolderModel *model : m_archiveParents)
  {
    if (!model)
      continue;
    model->close();
    delete model;
  }
  m_archiveParents.clear();
  m_archiveParentRoots.clear();
  m_archiveTemps.clear();
}

bool FilePanel::openNestedArchive(int row, ProgressState *progress, PromptDispatcher *prompts, QString *error,
                                  const QString &formatHint)
{
  if (!m_archiveModel || !m_archiveModel->isOpen() || row < 0)
    return false;
  ArchiveSession *session = m_archiveModel->session();
  if (!session)
  {
    if (error) *error = QStringLiteral("No archive is open");
    return false;
  }
  const ArchiveEntryInfo entry = m_archiveModel->entry(row);
  if (entry.isDir)
    return false;

  auto temp = std::make_shared<QTemporaryDir>();
  if (!temp->isValid())
  {
    if (error) *error = QStringLiteral("Cannot create a temporary directory");
    return false;
  }

  ExtractOptions eo;
  eo.outputDir = temp->path();
  eo.pathMode = 2; // no paths: a single file is extracted directly
  eo.overwriteMode = 1;
  eo.password = session->password();
  auto extractResult = session->extract({row}, eo, progress, prompts);
  if (!extractResult.ok)
  {
    if (error) *error = extractResult.error;
    return false;
  }

  QDirIterator it(temp->path(), QDir::Files, QDirIterator::Subdirectories);
  if (!it.hasNext())
  {
    if (error) *error = QStringLiteral("The extracted item is not a file");
    return false;
  }
  const QString extracted = it.next();

  auto *nested = new ArchiveFolderModel(this);
  QString openError;
  if (!nested->open(extracted, formatHint, progress, prompts, &openError, session->password()))
  {
    delete nested;
    if (error) *error = openError;
    return false;
  }

  const QString parentDisplay = displayPath();
  m_archiveParents.append(m_archiveModel);
  m_archiveParentRoots.append(m_archiveRootDisplay);
  m_archiveTemps.append(temp);
  connect(nested, &ArchiveFolderModel::pathChanged, this, [this, nested](const QString &) {
    if (m_mode == ArchiveMode && m_archiveModel == nested)
    {
      updatePathBar();
      emit pathChanged(displayPath());
      updateStatus();
    }
  });
  m_archiveModel = nested;
  m_archiveRootDisplay = parentDisplay + QLatin1Char('/') + entry.name;
  setMode(ArchiveMode);
  if (m_flatMode)
    setFlatMode(true);
  updatePathBar();
  emit pathChanged(displayPath());
  updateStatus();
  return true;
}

void FilePanel::closeArchive()
{
  clearNestedArchives();
  m_archiveRootDisplay.clear();
  if (m_archiveModel->isOpen())
    m_archiveModel->close();
  if (m_mode == ArchiveMode)
  {
    setMode(LocalMode);
    m_localModel->setPath(m_lastLocalPath.isEmpty() ? QDir::homePath() : m_lastLocalPath);
    updatePathBar();
  }
}

void FilePanel::refresh()
{
  if (m_mode == LocalMode)
    m_localModel->refresh();
  else if (m_mode == ArchiveMode)
    m_archiveModel->refresh();
  else
    m_rootModel->refresh();
  updateStatus();
}

bool FilePanel::goUp()
{
  if (m_mode == ArchiveMode)
  {
    if (m_archiveModel->up())
    {
      updatePathBar();
      emit pathChanged(displayPath());
      updateStatus();
      return true;
    }
    if (!m_archiveParents.isEmpty())
    {
      delete m_archiveModel;
      m_archiveModel = m_archiveParents.takeLast();
      m_archiveRootDisplay = m_archiveParentRoots.takeLast();
      m_archiveTemps.removeLast();
      setMode(ArchiveMode);
      updatePathBar();
      emit pathChanged(displayPath());
      updateStatus();
      return true;
    }
    return false;
  }
  if (m_mode == LocalMode)
  {
    QDir dir(m_localModel->path());
    if (dir.cdUp())
    {
      setLocalPath(dir.absolutePath());
      return true;
    }
  }
  return false;
}

void FilePanel::focusView()
{
  m_view->setFocus();
}

void FilePanel::focusPathBar()
{
  m_pathBar->setFocus();
  m_pathBar->lineEdit()->selectAll();
}

void FilePanel::setPanelId(int id)
{
  m_panelId = id;
}

void FilePanel::setActive(bool active)
{
  if (m_active == active)
    return;
  m_active = active;
  setProperty("active", active);
  if (m_status)
  {
    m_status->setProperty("active", active);
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
  }
  style()->unpolish(this);
  style()->polish(this);
}

void FilePanel::setDropHighlight(bool active)
{
  if (property("dropActive").toBool() == active)
    return;
  setProperty("dropActive", active);
  style()->unpolish(this);
  style()->polish(this);
}

void FilePanel::selectAllRows()
{
  QItemSelectionModel *selection = m_view->selectionModel();
  if (!selection || !m_view->model())
    return;
  const int rows = m_view->model()->rowCount();
  QItemSelection all;
  int first = -1;
  for (int row = 0; row < rows; ++row)
  {
    if (m_mode == LocalMode && m_localModel->isParentRow(row))
      continue;
    const QModelIndex left = m_view->model()->index(row, 0);
    const QModelIndex right = m_view->model()->index(row, m_view->model()->columnCount() - 1);
    if (!left.isValid())
      continue;
    all.select(left, right);
    if (first < 0)
      first = row;
  }
  selection->clearSelection();
  if (!all.isEmpty())
    selection->select(all, QItemSelectionModel::Select | QItemSelectionModel::Rows);
  if (first >= 0)
  {
    const QModelIndex index = m_view->model()->index(first, 0);
    selection->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    m_view->scrollTo(index);
  }
  updateStatus();
}

void FilePanel::selectLocalPaths(const QStringList &paths)
{
  if (m_mode != LocalMode || paths.isEmpty())
    return;
  QSet<QString> wanted;
  for (const QString &path : paths)
    wanted.insert(QFileInfo(path).absoluteFilePath());
  QItemSelectionModel *selection = m_view->selectionModel();
  if (!selection)
    return;
  QItemSelection selected;
  QModelIndex current;
  for (int row = 0; row < m_localModel->rowCount(); ++row)
  {
    const QString key = localKeyForRow(row);
    if (!wanted.contains(key))
      continue;
    const QModelIndex left = m_localModel->index(row, 0);
    const QModelIndex right = m_localModel->index(row, m_localModel->columnCount() - 1);
    selected.select(left, right);
    if (!current.isValid())
      current = left;
  }
  selection->clearSelection();
  if (!selected.isEmpty())
    selection->select(selected, QItemSelectionModel::Select | QItemSelectionModel::Rows);
  if (current.isValid())
  {
    selection->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
    m_view->scrollTo(current);
  }
  updateStatus();
}

void FilePanel::selectArchiveEntries(const QStringList &names)
{
  if (m_mode != ArchiveMode || names.isEmpty() || !m_archiveModel)
    return;
  QSet<QString> wanted;
  for (const QString &name : names)
  {
    QString clean = name;
    while (clean.endsWith(QLatin1Char('/')))
      clean.chop(1);
    wanted.insert(clean);
  }
  QItemSelectionModel *selection = m_view->selectionModel();
  if (!selection)
    return;
  QItemSelection selected;
  QModelIndex current;
  for (int row = 0; row < m_archiveModel->rowCount(); ++row)
  {
    const ArchiveEntryInfo &entry = m_archiveModel->entry(row);
    QString entryName = entry.name;
    while (entryName.endsWith(QLatin1Char('/')))
      entryName.chop(1);
    if (!wanted.contains(entryName))
      continue;
    const QModelIndex left = m_archiveModel->index(row, 0);
    const QModelIndex right = m_archiveModel->index(row, m_archiveModel->columnCount() - 1);
    selected.select(left, right);
    if (!current.isValid())
      current = left;
  }
  selection->clearSelection();
  if (!selected.isEmpty())
    selection->select(selected, QItemSelectionModel::Select | QItemSelectionModel::Rows);
  if (current.isValid())
  {
    selection->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
    m_view->scrollTo(current);
  }
  updateStatus();
}

PanelTransfer FilePanel::selectionTransfer() const
{
  PanelTransfer transfer;
  transfer.sourcePanelId = m_panelId;
  if (m_mode == LocalMode)
  {
    transfer.kind = PanelTransfer::Local;
    transfer.sourceRoot = m_localModel->path();
    transfer.localPaths = selectedLocalPaths();
    for (const QString &path : transfer.localPaths)
      transfer.names << QFileInfo(path).fileName();
  }
  else if (m_mode == ArchiveMode && m_archiveModel && m_archiveModel->session())
  {
    ArchiveSession *session = m_archiveModel->session();
    transfer.kind = PanelTransfer::Archive;
    transfer.sourceRoot = session->archivePath();
    transfer.archivePrefix = session->relativePath();
    transfer.archiveRows = selectedArchiveRows();
    for (int row : transfer.archiveRows)
    {
      const ArchiveEntryInfo &entry = m_archiveModel->entry(row);
      QString name = entry.name;
      while (name.endsWith(QLatin1Char('/')))
        name.chop(1);
      transfer.names << name;
      transfer.archivePaths << (entry.path.isEmpty() ? name : entry.path);
    }
  }
  return transfer;
}

PanelDropTarget FilePanel::dropTargetForPosition(const QPoint &viewportPos) const
{
  PanelDropTarget target;
  const QModelIndex index = m_view->indexAt(viewportPos);
  if (m_mode == LocalMode)
  {
    target.localDir = m_localModel->path();
    if (index.isValid())
    {
      const QFileInfo info = m_localModel->fileInfo(index.row());
      if (info.isDir())
        target.localDir = info.absoluteFilePath();
    }
  }
  else if (m_mode == ArchiveMode && m_archiveModel && m_archiveModel->isOpen())
  {
    target.archive = true;
    target.archivePrefix = archiveRelativePath();
    if (index.isValid())
    {
      const ArchiveEntryInfo &entry = m_archiveModel->entry(index.row());
      if (entry.isDir)
      {
        QString name = entry.name;
        while (name.endsWith(QLatin1Char('/')))
          name.chop(1);
        if (!name.isEmpty() && name != QLatin1String(".") && name != QLatin1String(".."))
        {
          QString prefix = target.archivePrefix;
          while (prefix.endsWith(QLatin1Char('/')))
            prefix.chop(1);
          target.archivePrefix = prefix.isEmpty() ? name : prefix + QLatin1Char('/') + name;
        }
      }
    }
  }
  return target;
}

QString FilePanel::archiveRelativePath() const
{
  return (m_archiveModel && m_archiveModel->session())
      ? m_archiveModel->session()->relativePath() : QString();
}

bool FilePanel::enterArchivePath(const QString &relativePath)
{
  if (m_mode != ArchiveMode || !m_archiveModel || !m_archiveModel->session())
    return false;
  if (!m_archiveModel->session()->enterPath(relativePath))
    return false;
  m_archiveModel->refresh();
  updatePathBar();
  emit pathChanged(displayPath());
  updateStatus();
  return true;
}

bool FilePanel::isArchive() const
{
  return m_mode == ArchiveMode && m_archiveModel && m_archiveModel->isOpen();
}

bool FilePanel::reloadArchive(ProgressState *progress, PromptDispatcher *prompts, QString *error)
{
  if (m_mode != ArchiveMode || !m_archiveModel || !m_archiveModel->isOpen())
    return false;
  ArchiveSession *session = m_archiveModel->session();
  if (!session)
    return false;

  const QString path = session->archivePath();
  const QString formatHint = session->formatName();
  const QString password = session->password();
  const QString prefix = session->relativePath();
  QString err;
  if (!m_archiveModel->open(path, formatHint, progress, prompts, &err, password))
  {
    if (error) *error = err;
    return false;
  }
  if (!prefix.isEmpty())
    m_archiveModel->session()->enterPath(prefix);
  if (m_flatMode)
    m_archiveModel->setFlatMode(true);
  m_archiveModel->refresh();
  updatePathBar();
  emit pathChanged(displayPath());
  updateStatus();
  return true;
}

QString FilePanel::localPath() const { return m_localModel->path(); }

QString FilePanel::displayPath() const
{
  if (m_mode == ArchiveMode)
  {
    if (!m_archiveModel)
      return m_archiveRootDisplay;
    const QString relative = m_archiveModel->session() ? m_archiveModel->session()->relativePath() : QString();
    if (relative.isEmpty())
      return m_archiveRootDisplay.isEmpty() ? m_archiveModel->path() : m_archiveRootDisplay;
    return (m_archiveRootDisplay.isEmpty() ? m_archiveModel->path() : m_archiveRootDisplay)
        + QLatin1Char('/') + relative;
  }
  if (m_mode == RootMode) return QStringLiteral("Computer");
  return m_localModel->path();
}

QStringList FilePanel::selectedLocalPaths() const
{
  QStringList out;
  if (m_mode != LocalMode) return out;
  const auto rows = m_view->selectionModel()->selectedRows(0);
  for (const QModelIndex &idx : rows)
  {
    if (m_localModel->isParentRow(idx.row())) continue;
    const QFileInfo fi = m_localModel->fileInfo(idx.row());
    if (!fi.absoluteFilePath().isEmpty())
      out << fi.absoluteFilePath();
  }
  out.removeDuplicates();
  return out;
}

QVector<int> FilePanel::selectedArchiveRows() const
{
  QVector<int> rows;
  if (m_mode != ArchiveMode) return rows;
  const auto selected = m_view->selectionModel()->selectedRows(0);
  rows.reserve(selected.size());
  for (const QModelIndex &idx : selected)
    rows.append(idx.row());
  std::sort(rows.begin(), rows.end());
  rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
  return rows;
}

int FilePanel::currentRow() const
{
  const QModelIndex idx = m_view->currentIndex();
  return idx.isValid() ? idx.row() : -1;
}

void FilePanel::invertSelection()
{
  QItemSelectionModel *selection = m_view->selectionModel();
  if (!selection || !m_view->model())
    return;
  const int rows = m_view->model()->rowCount();
  const int columns = m_view->model()->columnCount();
  QItemSelection inverted;
  QModelIndex current;
  for (int row = 0; row < rows; ++row)
  {
    if (m_mode == LocalMode && m_localModel->isParentRow(row))
      continue;
    if (selection->isRowSelected(row, QModelIndex()))
      continue;
    const QModelIndex left = m_view->model()->index(row, 0);
    const QModelIndex right = m_view->model()->index(row, columns - 1);
    if (!left.isValid())
      continue;
    inverted.select(left, right);
    if (!current.isValid())
      current = left;
  }
  selection->clearSelection();
  if (!inverted.isEmpty())
    selection->select(inverted, QItemSelectionModel::Select | QItemSelectionModel::Rows);
  if (current.isValid())
  {
    selection->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
    m_view->scrollTo(current);
  }
  updateStatus();
}

void FilePanel::onCustomContextMenu(const QPoint &pos)
{
  const QModelIndex index = m_view->indexAt(pos);
  if (index.isValid())
  {
    if (!m_view->selectionModel()->isRowSelected(index.row(), QModelIndex()))
    {
      m_view->selectionModel()->clearSelection();
      m_view->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    m_view->setCurrentIndex(index);
  }
  emit activeRequested();
  emit contextMenuRequested(m_view->viewport()->mapToGlobal(pos));
}

void FilePanel::onPathEntered()
{
  if (m_mode != LocalMode)
    return;
  const QString text = m_pathBar->currentText().trimmed();
  if (text.isEmpty()) return;
  setLocalPath(text);
}

void FilePanel::onActivated(const QModelIndex &index)
{
  if (!index.isValid()) return;
  if (m_mode == RootMode)
  {
    const QString path = m_rootModel->pathForRow(index.row());
    if (!path.isEmpty()) setLocalPath(path);
    return;
  }
  if (m_mode == ArchiveMode)
  {
    const ArchiveEntryInfo &e = m_archiveModel->entry(index.row());
    if (e.isDir)
    {
      if (m_archiveModel->enter(index.row()))
      {
        updatePathBar();
        emit pathChanged(displayPath());
        updateStatus();
      }
    }
    else
    {
      static const QStringList archiveExts = {
          QStringLiteral("7z"), QStringLiteral("zip"), QStringLiteral("rar"),
          QStringLiteral("tar"), QStringLiteral("gz"), QStringLiteral("tgz"),
          QStringLiteral("bz2"), QStringLiteral("xz"), QStringLiteral("cab"),
          QStringLiteral("iso"), QStringLiteral("wim"), QStringLiteral("arj"),
          QStringLiteral("lzh"), QStringLiteral("zst")};
      if (archiveExts.contains(QFileInfo(e.name).suffix().toLower()))
        emit nestedArchiveRequested(index.row());
    }
    return;
  }
  const QFileInfo fi = m_localModel->fileInfo(index.row());
  if (fi.isDir())
  {
    setLocalPath(fi.absoluteFilePath());
    return;
  }
  const QString path = fi.absoluteFilePath();
  emit openArchiveRequested(path);
}

void FilePanel::onSelectionChanged()
{
  updateStatus();
  if (!m_restoringSelection)
    emit activeRequested();
}

static QString panelSizeText(quint64 bytes)
{
  return QLocale().formattedDataSize(qint64(bytes), 1, QLocale::DataSizeTraditionalFormat);
}

void FilePanel::updateStatus()
{
  if (m_mode == LocalMode)
  {
    const int total = m_localModel->rowCount();
    const int selected = selectedLocalPaths().size();
    quint64 selectedBytes = 0;
    if (selected > 0)
    {
      const auto rows = m_view->selectionModel()->selectedRows(0);
      for (const QModelIndex &idx : rows)
      {
        if (m_localModel->isParentRow(idx.row()))
          continue;
        const QFileInfo info = m_localModel->fileInfo(idx.row());
        if (info.isFile() && !info.isSymLink())
          selectedBytes += quint64(info.size());
      }
    }
    QString text = QStringLiteral("%1 item(s)   %2 selected").arg(total).arg(selected);
    if (selectedBytes > 0)
      text += QStringLiteral("   %1").arg(panelSizeText(selectedBytes));
    m_status->setText(text);
  }
  else if (m_mode == ArchiveMode)
  {
    const int total = m_archiveModel->rowCount();
    const QVector<int> rows = selectedArchiveRows();
    const int selected = rows.size();
    quint64 selectedBytes = 0;
    for (int row : rows)
      selectedBytes += m_archiveModel->entry(row).size;
    QString text = QStringLiteral("%1 item(s)   %2 selected").arg(total).arg(selected);
    if (selectedBytes > 0)
      text += QStringLiteral("   %1").arg(panelSizeText(selectedBytes));
    text += QStringLiteral("   %1").arg(displayPath());
    m_status->setText(text);
  }
  else
  {
    m_status->setText(QStringLiteral("Mounted filesystems"));
  }
}

void FilePanel::updatePathBar()
{
  const QString path = displayPath();
  if (m_pathBar->count() == 0 || m_pathBar->itemText(0) != path)
  {
    m_pathBar->blockSignals(true);
    m_pathBar->insertItem(0, path);
    while (m_pathBar->count() > 20)
      m_pathBar->removeItem(m_pathBar->count() - 1);
    m_pathBar->setCurrentIndex(0);
    m_pathBar->blockSignals(false);
  }
  m_pathBar->setToolTip(path);
}

void FilePanel::connectSelectionModel()
{
  if (QItemSelectionModel *selection = m_view->selectionModel())
  {
    disconnect(selection, nullptr, this, nullptr);
    connect(selection, &QItemSelectionModel::selectionChanged, this,
            &FilePanel::onSelectionChanged);
    connect(selection, &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &, const QModelIndex &) { updateStatus(); });
  }
}

void FilePanel::connectModelSignals()
{
  connect(m_localModel, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
    if (m_mode == LocalMode)
      saveLocalSelection();
  });
  connect(m_localModel, &QAbstractItemModel::modelReset, this, [this] {
    if (m_mode == LocalMode)
      scheduleLocalSelectionRestore();
  });
  connect(m_archiveModel, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
    if (m_mode == ArchiveMode)
      saveArchiveSelection();
  });
  connect(m_archiveModel, &QAbstractItemModel::modelReset, this, [this] {
    if (m_mode == ArchiveMode)
      scheduleArchiveSelectionRestore();
  });
}

QString FilePanel::localKeyForRow(int row) const
{
  return m_localModel->fileInfo(row).absoluteFilePath();
}

QString FilePanel::archiveKeyForRow(int row) const
{
  const ArchiveEntryInfo &entry = m_archiveModel->entry(row);
  if (!entry.path.isEmpty())
    return entry.path;
  QString name = entry.name;
  while (name.endsWith(QLatin1Char('/')))
    name.chop(1);
  const QString prefix = archiveRelativePath();
  return prefix.isEmpty() ? name : prefix + QLatin1Char('/') + name;
}

void FilePanel::saveLocalSelection()
{
  m_savedLocalRoot = m_localModel->path();
  m_savedLocalSelection.clear();
  m_savedLocalCurrent.clear();
  QItemSelectionModel *selection = m_view->selectionModel();
  if (!selection || m_mode != LocalMode)
    return;
  const auto rows = selection->selectedRows(0);
  for (const QModelIndex &index : rows)
  {
    if (m_localModel->isParentRow(index.row()))
      continue;
    const QString key = localKeyForRow(index.row());
    if (!key.isEmpty())
      m_savedLocalSelection.insert(key);
  }
  const QModelIndex current = m_view->currentIndex();
  if (current.isValid() && !m_localModel->isParentRow(current.row()))
    m_savedLocalCurrent = localKeyForRow(current.row());
}

void FilePanel::saveArchiveSelection()
{
  if (!m_archiveModel || !m_archiveModel->session())
  {
    m_savedArchiveSelection.clear();
    m_savedArchivePrefix.clear();
    m_savedArchiveRoot.clear();
    m_savedArchiveCurrent.clear();
    return;
  }
  m_savedArchiveRoot = m_archiveModel->session()->archivePath();
  m_savedArchivePrefix = archiveRelativePath();
  m_savedArchiveSelection.clear();
  m_savedArchiveCurrent.clear();
  QItemSelectionModel *selection = m_view->selectionModel();
  if (!selection || m_mode != ArchiveMode)
    return;
  const auto rows = selection->selectedRows(0);
  for (const QModelIndex &index : rows)
  {
    const QString key = archiveKeyForRow(index.row());
    if (!key.isEmpty())
      m_savedArchiveSelection.insert(key);
  }
  const QModelIndex current = m_view->currentIndex();
  if (current.isValid())
    m_savedArchiveCurrent = archiveKeyForRow(current.row());
}

void FilePanel::scheduleLocalSelectionRestore()
{
  QTimer::singleShot(0, this, &FilePanel::restoreLocalSelection);
}

void FilePanel::scheduleArchiveSelectionRestore()
{
  QTimer::singleShot(0, this, &FilePanel::restoreArchiveSelection);
}

void FilePanel::restoreLocalSelection()
{
  if (m_mode != LocalMode || m_restoringSelection)
    return;
  if (m_savedLocalRoot != m_localModel->path())
    return;
  QItemSelectionModel *selection = m_view->selectionModel();
  if (!selection)
    return;

  m_restoringSelection = true;
  QItemSelection selected;
  QModelIndex current;
  for (int row = 0; row < m_localModel->rowCount(); ++row)
  {
    const QString key = localKeyForRow(row);
    if (m_savedLocalSelection.contains(key))
    {
      const QModelIndex left = m_localModel->index(row, 0);
      const QModelIndex right = m_localModel->index(row, m_localModel->columnCount() - 1);
      selected.select(left, right);
    }
    if (!m_savedLocalCurrent.isEmpty() && key == m_savedLocalCurrent)
      current = m_localModel->index(row, 0);
  }
  selection->clearSelection();
  if (!selected.isEmpty())
    selection->select(selected, QItemSelectionModel::Select | QItemSelectionModel::Rows);
  if (current.isValid())
  {
    selection->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
    m_view->scrollTo(current);
  }
  m_savedLocalSelection.clear();
  m_savedLocalCurrent.clear();
  m_savedLocalRoot.clear();
  m_restoringSelection = false;
  updateStatus();
}

void FilePanel::restoreArchiveSelection()
{
  if (m_mode != ArchiveMode || m_restoringSelection)
    return;
  if (!m_archiveModel || !m_archiveModel->session())
    return;
  if (m_savedArchiveRoot != m_archiveModel->session()->archivePath())
    return;
  if (m_savedArchivePrefix != archiveRelativePath())
    return;
  QItemSelectionModel *selection = m_view->selectionModel();
  if (!selection)
    return;

  m_restoringSelection = true;
  QItemSelection selected;
  QModelIndex current;
  for (int row = 0; row < m_archiveModel->rowCount(); ++row)
  {
    const QString key = archiveKeyForRow(row);
    if (m_savedArchiveSelection.contains(key))
    {
      const QModelIndex left = m_archiveModel->index(row, 0);
      const QModelIndex right = m_archiveModel->index(row, m_archiveModel->columnCount() - 1);
      selected.select(left, right);
    }
    if (!m_savedArchiveCurrent.isEmpty() && key == m_savedArchiveCurrent)
      current = m_archiveModel->index(row, 0);
  }
  selection->clearSelection();
  if (!selected.isEmpty())
    selection->select(selected, QItemSelectionModel::Select | QItemSelectionModel::Rows);
  if (current.isValid())
  {
    selection->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
    m_view->scrollTo(current);
  }
  m_savedArchiveSelection.clear();
  m_savedArchiveCurrent.clear();
  m_savedArchiveRoot.clear();
  m_savedArchivePrefix.clear();
  m_restoringSelection = false;
  updateStatus();
}

bool FilePanel::eventFilter(QObject *watched, QEvent *event)
{
  if (event->type() == QEvent::ContextMenu && (watched == m_view || watched == m_view->viewport()))
  {
    auto *context = static_cast<QContextMenuEvent *>(event);
    const QPoint pos = watched == m_view
        ? m_view->viewport()->mapFrom(m_view, context->pos()) : context->pos();
    onCustomContextMenu(pos);
    context->accept();
    return true;
  }

  if (event->type() == QEvent::FocusIn &&
      (watched == m_view || watched == m_view->viewport() ||
       watched == m_pathBar || watched == m_pathBar->lineEdit()))
  {
    emit activeRequested();
  }

  if (event->type() == QEvent::MouseButtonPress &&
      (watched == m_view || watched == m_view->viewport()))
  {
    emit activeRequested();
  }

  return QWidget::eventFilter(watched, event);
}
