#include "FilePanel.h"
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
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QMessageBox>
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

  m_view = new QTableView(this);
  m_view->setObjectName(QStringLiteral("fileView"));
  m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
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
  m_view->setDragEnabled(true);
  m_view->setAcceptDrops(true);
  m_view->viewport()->setAcceptDrops(true);
  m_view->installEventFilter(this);
  m_view->viewport()->installEventFilter(this);
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
  connect(m_view, &QTableView::doubleClicked, this, &FilePanel::onActivated);
  connect(m_view, &QTableView::clicked, this, &FilePanel::activeRequested);
  connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &FilePanel::onSelectionChanged);
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

bool FilePanel::isArchive() const
{
  return m_mode == ArchiveMode && m_archiveModel && m_archiveModel->isOpen();
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
  QItemSelection inverted;
  for (int row = 0; row < rows; ++row)
  {
    const QModelIndex index = m_view->model()->index(row, 0);
    if (!selection->isRowSelected(row, QModelIndex()))
      inverted.select(index, index);
  }
  selection->clearSelection();
  if (!inverted.isEmpty())
    selection->select(inverted, QItemSelectionModel::Select | QItemSelectionModel::Rows);
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
      m_archiveModel->enter(index.row());
      emit pathChanged(displayPath());
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
  emit activeRequested();
}

void FilePanel::updateStatus()
{
  if (m_mode == LocalMode)
  {
    const int total = m_localModel->rowCount();
    const int selected = m_view->selectionModel()->selectedRows(0).size();
    m_status->setText(QStringLiteral("%1 item(s)   %2 selected").arg(total).arg(selected));
  }
  else if (m_mode == ArchiveMode)
  {
    const int total = m_archiveModel->rowCount();
    const int selected = m_view->selectionModel()->selectedRows(0).size();
    m_status->setText(QStringLiteral("%1 item(s)   %2 selected   %3")
                          .arg(total).arg(selected).arg(displayPath()));
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

static bool copyDropPath(const QString &src, const QString &dst, QString *error)
{
  const QFileInfo fi(src);
  if (fi.isSymLink())
  {
    if (QFile::exists(dst)) { if (error) *error = QStringLiteral("Destination already exists: %1").arg(dst); return false; }
    if (!QFile::link(fi.symLinkTarget(), dst)) { if (error) *error = QStringLiteral("Cannot create link: %1").arg(src); return false; }
    return true;
  }
  if (fi.isDir())
  {
    if (!QDir().mkpath(dst)) { if (error) *error = dst; return false; }
    QDirIterator it(src, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    while (it.hasNext())
    {
      const QString child = it.next();
      if (!copyDropPath(child, dst + QLatin1Char('/') + QFileInfo(child).fileName(), error))
        return false;
    }
    return true;
  }
  if (QFile::exists(dst)) { if (error) *error = QStringLiteral("Destination already exists: %1").arg(dst); return false; }
  if (!QFile::copy(src, dst)) { if (error) *error = src; return false; }
  return true;
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

  if (watched == m_view && event->type() == QEvent::FocusIn)
    emit activeRequested();

  if ((watched == m_view || watched == m_view->viewport()) && event->type() == QEvent::DragEnter)
  {
    auto *de = static_cast<QDragEnterEvent *>(event);
    if (de->mimeData()->hasUrls())
    {
      de->acceptProposedAction();
      return true;
    }
  }
  if ((watched == m_view || watched == m_view->viewport()) && event->type() == QEvent::DragMove)
  {
    auto *de = static_cast<QDragMoveEvent *>(event);
    if (de->mimeData()->hasUrls())
    {
      de->acceptProposedAction();
      return true;
    }
  }
  if (watched == m_view->viewport() && event->type() == QEvent::Drop)
  {
    auto *de = static_cast<QDropEvent *>(event);
    QStringList files;
    for (const QUrl &url : de->mimeData()->urls())
      if (url.isLocalFile()) files << url.toLocalFile();
    if (files.isEmpty()) return false;
    const bool move = de->proposedAction() == Qt::MoveAction;
    if (m_mode == ArchiveMode)
    {
      emit filesDropped(files, move);
      de->acceptProposedAction();
      return true;
    }
    if (m_mode == LocalMode)
    {
      for (const QString &src : files)
      {
        const QString dst = m_localModel->path() + QLatin1Char('/') + QFileInfo(src).fileName();
        QString error;
        if (!copyDropPath(src, dst, &error))
        {
          QMessageBox box(QMessageBox::NoIcon, QStringLiteral("Drop"),
                          QStringLiteral("Cannot copy: %1").arg(error), QMessageBox::Ok, this);
          box.exec();
        }
        else if (move)
        {
          const QFileInfo fi(src);
          if (fi.isSymLink() || !fi.isDir())
            QFile::remove(src);
          else
            QDir(src).removeRecursively();
        }
      }
      m_localModel->refresh();
      de->acceptProposedAction();
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}
