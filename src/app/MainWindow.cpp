#include "MainWindow.h"
#include "ui/widgets/FilePanel.h"
#include "ui/dialogs/ExtractDialog.h"
#include "ui/dialogs/CompressDialog.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/dialogs/HashResultDialog.h"
#include "ui/dialogs/OptionsDialog.h"
#include "ui/dialogs/AboutDialog.h"
#include "ui/dialogs/BenchmarkDialog.h"
#include "backend/BenchmarkOperation.h"
#include "backend/HashOperation.h"
#include "models/ArchiveFolderModel.h"
#include "models/LocalFsModel.h"
#include "backend/ProgressState.h"
#include "backend/PromptDispatcher.h"
#include "backend/CompressOperation.h"
#include "backend/InputDialog.h"
#include "backend/LocalFileOperation.h"
#include "backend/SevenZipEngine.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QProcess>
#include <QFile>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>
#include <QDirIterator>
#include <QFile>
#include <unistd.h>
#include <QDesktopServices>
#include <QDir>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTableView>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

static bool removeLocalPath(const QString &path);

static bool isArchiveFile(const QString &path)
{
  static const QStringList exts = {
      QStringLiteral("7z"), QStringLiteral("zip"), QStringLiteral("rar"),
      QStringLiteral("tar"), QStringLiteral("gz"), QStringLiteral("tgz"),
      QStringLiteral("bz2"), QStringLiteral("xz"), QStringLiteral("cab"),
      QStringLiteral("iso"), QStringLiteral("wim"), QStringLiteral("arj"),
      QStringLiteral("lzh"), QStringLiteral("zst"), QStringLiteral("lzma"),
      QStringLiteral("z")};
  return exts.contains(QFileInfo(path).suffix().toLower());
}

class OperationThread : public QThread
{
public:
  std::function<OperationResult()> fn;
  OperationResult result;
  void run() override
  {
    try
    {
      result = fn();
    }
    catch (const std::exception &e)
    {
      result.error = QString::fromUtf8(e.what());
    }
    catch (...)
    {
      result.error = QStringLiteral("Unknown error");
    }
  }
};

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
  setWindowTitle(QStringLiteral("7zipLX"));
  setMinimumSize(900, 560);

  m_prompts = new PromptDispatcher(this);

  auto *central = new QWidget(this);
  auto *layout = new QVBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  m_splitter = new QSplitter(Qt::Horizontal, central);
  m_left = new FilePanel(m_splitter);
  m_right = new FilePanel(m_splitter);
  m_left->setPanelId(0);
  m_right->setPanelId(1);
  m_splitter->addWidget(m_left);
  m_splitter->addWidget(m_right);
  m_splitter->setStretchFactor(0, 1);
  m_splitter->setStretchFactor(1, 1);
  layout->addWidget(m_splitter);
  setCentralWidget(central);

  m_status = new QLabel(QStringLiteral("Ready"), this);
  statusBar()->addWidget(m_status, 1);

  auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
  auto *openAction = fileMenu->addAction(QStringLiteral("&Open Archive..."), this, &MainWindow::openArchiveDialog);
  openAction->setShortcut(QKeySequence::Open);
  fileMenu->addSeparator();
  m_addAction = fileMenu->addAction(QStringLiteral("&Add to Archive..."), this, &MainWindow::addToArchive);
  m_addAction->setShortcut(QKeySequence(QStringLiteral("Alt+A")));
  m_extractAction = fileMenu->addAction(QStringLiteral("&Extract..."), this, &MainWindow::extractArchive);
  m_extractAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
  m_testAction = fileMenu->addAction(QStringLiteral("&Test Archive"), this, &MainWindow::testArchive);
  m_testAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
  m_closeArchiveAction = fileMenu->addAction(QStringLiteral("&Close Archive"), this, &MainWindow::closeArchive);
  m_closeArchiveAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));
  fileMenu->addSeparator();
  fileMenu->addAction(QStringLiteral("E&xit"), this, &QWidget::close)->setShortcut(QKeySequence::Quit);

  auto *editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
  editMenu->addAction(QStringLiteral("Select &All"), this, &MainWindow::selectAll)->setShortcut(QKeySequence::SelectAll);
  editMenu->addSeparator();
  editMenu->addAction(QStringLiteral("Cu&t"), this, &MainWindow::clipboardCut)->setShortcut(QKeySequence::Cut);
  editMenu->addAction(QStringLiteral("&Copy"), this, &MainWindow::clipboardCopy)->setShortcut(QKeySequence::Copy);
  editMenu->addAction(QStringLiteral("&Paste"), this, &MainWindow::clipboardPaste)->setShortcut(QKeySequence::Paste);
  editMenu->addSeparator();
  editMenu->addAction(QStringLiteral("&Refresh"), this, &MainWindow::refreshActive);
  editMenu->addSeparator();
  editMenu->addAction(QStringLiteral("&Rename..."), this, &MainWindow::renameSelected)->setShortcut(QKeySequence(Qt::Key_F2));
  editMenu->addAction(QStringLiteral("&Copy to Other Panel"), this, &MainWindow::copyToOtherPanel)->setShortcut(QKeySequence(Qt::Key_F5));
  editMenu->addAction(QStringLiteral("&Move to Other Panel"), this, &MainWindow::moveToOtherPanel)->setShortcut(QKeySequence(Qt::Key_F6));
  editMenu->addAction(QStringLiteral("Copy &To..."), this, &MainWindow::copySelected)->setShortcut(QKeySequence(QStringLiteral("Ctrl+F5")));
  editMenu->addAction(QStringLiteral("Move T&o..."), this, &MainWindow::moveSelected)->setShortcut(QKeySequence(QStringLiteral("Ctrl+F6")));
  editMenu->addAction(QStringLiteral("&Delete"), this, &MainWindow::deleteSelected)->setShortcut(QKeySequence(Qt::Key_Delete));
  editMenu->addSeparator();
  editMenu->addAction(QStringLiteral("Create &Folder..."), this, &MainWindow::createFolder)->setShortcut(QKeySequence(Qt::Key_F7));
  editMenu->addAction(QStringLiteral("Create &File..."), this, &MainWindow::createFile)->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
  editMenu->addAction(QStringLiteral("P&roperties"), this, &MainWindow::showProperties)->setShortcut(QKeySequence(QStringLiteral("Alt+Return")));
  fileMenu->addSeparator();
  fileMenu->addAction(QStringLiteral("Create &Link..."), this, &MainWindow::createLink);
  fileMenu->addAction(QStringLiteral("&View"), this, &MainWindow::openExternalView)->setShortcut(QKeySequence(Qt::Key_F3));
  fileMenu->addAction(QStringLiteral("&Edit"), this, &MainWindow::openExternalEdit)->setShortcut(QKeySequence(Qt::Key_F4));
  fileMenu->addAction(QStringLiteral("Di&ff"), this, &MainWindow::diffSelected);
  fileMenu->addAction(QStringLiteral("&Comment..."), this, &MainWindow::editComment)->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
  fileMenu->addAction(QStringLiteral("&Split File..."), this, &MainWindow::splitFile);
  fileMenu->addAction(QStringLiteral("&Combine Files..."), this, &MainWindow::combineFiles);

  auto *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
  viewMenu->addAction(QStringLiteral("&Up One Level"), this, &MainWindow::goUp)->setShortcut(QKeySequence(QStringLiteral("Backspace")));
  viewMenu->addAction(QStringLiteral("&Computer"), this, &MainWindow::showRoot);
  viewMenu->addSeparator();
  m_showHiddenAction = viewMenu->addAction(QStringLiteral("Show &Hidden Files"));
  m_showHiddenAction->setCheckable(true);
  m_showDotsAction = viewMenu->addAction(QStringLiteral("Show &Parent Entry (..)"));
  m_showDotsAction->setCheckable(true);
  m_flatViewAction = viewMenu->addAction(QStringLiteral("&Flat View"));
  m_flatViewAction->setCheckable(true);
  connect(m_showHiddenAction, &QAction::toggled, this, [this](bool on) {
    if (m_left) m_left->setShowHidden(on);
    if (m_right) m_right->setShowHidden(on);
  });
  connect(m_showDotsAction, &QAction::toggled, this, [this](bool on) {
    if (m_left) m_left->setShowDots(on);
    if (m_right) m_right->setShowDots(on);
  });
  connect(m_flatViewAction, &QAction::toggled, this, [this](bool on) {
    if (m_left) m_left->setFlatMode(on);
    if (m_right) m_right->setFlatMode(on);
  });

  auto *favMenu = menuBar()->addMenu(QStringLiteral("&Favorites"));
  favMenu->addAction(QStringLiteral("&Add Current Folder"), this, &MainWindow::addFavorite);
  favMenu->addSeparator();
  connect(favMenu, &QMenu::aboutToShow, this, [this, favMenu] {
    while (!favMenu->actions().isEmpty())
      delete favMenu->actions().first();
    favMenu->addAction(QStringLiteral("&Add Current Folder"), this, &MainWindow::addFavorite);
    favMenu->addAction(QStringLiteral("&Clear Favorites"), this, &MainWindow::clearFavorites);
    favMenu->addSeparator();
    QSettings s;
    const QStringList favs = s.value(QStringLiteral("favorites"), QStringList()).toStringList();
    for (const QString &path : favs)
      favMenu->addAction(path, this, [this, path] { if (m_active) m_active->setLocalPath(path); });
  });

  auto *toolsMenu = menuBar()->addMenu(QStringLiteral("&Tools"));
  toolsMenu->addAction(QStringLiteral("&Benchmark..."), this, &MainWindow::showBenchmark);
  toolsMenu->addAction(QStringLiteral("&Hash..."), this, &MainWindow::calculateHash)->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));
  toolsMenu->addSeparator();
  toolsMenu->addAction(QStringLiteral("&Options..."), this, &MainWindow::showOptions);

  auto *helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
  helpMenu->addAction(QStringLiteral("&About 7zipLX..."), this, &MainWindow::showAbout);

  auto *toolbar = addToolBar(QStringLiteral("Main"));
  toolbar->setObjectName(QStringLiteral("mainToolbar"));
  toolbar->setMovable(false);
  toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  toolbar->setIconSize(QSize(18, 18));

  auto themeIcon = [](const char *path) { return QIcon(QString::fromLatin1(path)); };
  openAction->setIcon(themeIcon(":/icons/toolbar/open.svg"));
  m_addAction->setIcon(themeIcon(":/icons/toolbar/add.svg"));
  m_extractAction->setIcon(themeIcon(":/icons/toolbar/extract.svg"));
  m_testAction->setIcon(themeIcon(":/icons/toolbar/test.svg"));

  auto *brand = new QLabel(QStringLiteral("7zipLX"), toolbar);
  brand->setObjectName(QStringLiteral("brand"));
  toolbar->addWidget(brand);
  toolbar->addSeparator();
  toolbar->addAction(openAction);
  toolbar->addSeparator();
  toolbar->addAction(m_addAction);
  toolbar->addAction(m_extractAction);
  toolbar->addAction(m_testAction);
  toolbar->addSeparator();
  auto *upAction = toolbar->addAction(QStringLiteral("Up"), this, &MainWindow::goUp);
  upAction->setIcon(themeIcon(":/icons/toolbar/up.svg"));
  auto *refreshAction = toolbar->addAction(QStringLiteral("Refresh"), this, &MainWindow::refreshActive);
  refreshAction->setIcon(themeIcon(":/icons/toolbar/refresh.svg"));

  connect(m_left, &FilePanel::activeRequested, this, [this] { setActivePanel(m_left); });
  connect(m_right, &FilePanel::activeRequested, this, [this] { setActivePanel(m_right); });
  connect(m_left, &FilePanel::openArchiveRequested, this, &MainWindow::openArchiveRequested);
  connect(m_right, &FilePanel::openArchiveRequested, this, &MainWindow::openArchiveRequested);
  connect(m_left, &FilePanel::transferRequested, this, &MainWindow::handleTransfer);
  connect(m_right, &FilePanel::transferRequested, this, &MainWindow::handleTransfer);
  connect(m_left, &FilePanel::focusOtherPanelRequested, this, &MainWindow::focusOtherPanel);
  connect(m_right, &FilePanel::focusOtherPanelRequested, this, &MainWindow::focusOtherPanel);
  connect(m_left, &FilePanel::filesMovedExternally, this, [this](const QStringList &) { m_left->refresh(); });
  connect(m_right, &FilePanel::filesMovedExternally, this, [this](const QStringList &) { m_right->refresh(); });
  connect(m_left, &FilePanel::nestedArchiveRequested, this, &MainWindow::openNestedArchive);
  connect(m_right, &FilePanel::nestedArchiveRequested, this, &MainWindow::openNestedArchive);
  connect(m_left, &FilePanel::contextMenuRequested, this, &MainWindow::showPanelContextMenu);
  connect(m_right, &FilePanel::contextMenuRequested, this, &MainWindow::showPanelContextMenu);
  connect(m_left, &FilePanel::pathChanged, this, [this](const QString &p) {
    if (m_active == m_left) m_status->setText(p);
  });
  connect(m_right, &FilePanel::pathChanged, this, [this](const QString &p) {
    if (m_active == m_right) m_status->setText(p);
  });

  setActivePanel(m_left);
  loadSettings();
  updateActions();
}

MainWindow::~MainWindow() = default;

void MainWindow::loadSettings()
{
  QSettings s;
  const QByteArray geo = s.value(QStringLiteral("window/geometry")).toByteArray();
  if (!geo.isEmpty()) restoreGeometry(geo);
  const QByteArray state = s.value(QStringLiteral("window/splitter")).toByteArray();
  if (!state.isEmpty()) m_splitter->restoreState(state);
  const QString left = s.value(QStringLiteral("window/leftPath"), QDir::homePath()).toString();
  const QString right = s.value(QStringLiteral("window/rightPath"), QDir::homePath()).toString();
  const bool showHidden = s.value(QStringLiteral("view/showHidden"), false).toBool();
  const bool showDots = s.value(QStringLiteral("view/showDots"), true).toBool();
  const bool showGrid = s.value(QStringLiteral("view/showGrid"), false).toBool();
  const bool flatView = s.value(QStringLiteral("view/flatView"), false).toBool();
  if (m_flatViewAction) m_flatViewAction->setChecked(flatView);
  if (m_left) m_left->setShowGrid(showGrid);
  if (m_right) m_right->setShowGrid(showGrid);
  if (m_showHiddenAction) m_showHiddenAction->setChecked(showHidden);
  if (m_showDotsAction) m_showDotsAction->setChecked(showDots);
  if (!left.isEmpty()) m_left->setLocalPath(left);
  if (!right.isEmpty()) m_right->setLocalPath(right);
}

void MainWindow::saveSettings()
{
  QSettings s;
  s.setValue(QStringLiteral("window/geometry"), saveGeometry());
  s.setValue(QStringLiteral("window/splitter"), m_splitter->saveState());
  s.setValue(QStringLiteral("window/leftPath"), m_left->localPath());
  s.setValue(QStringLiteral("window/rightPath"), m_right->localPath());
  s.setValue(QStringLiteral("view/showHidden"), m_showHiddenAction && m_showHiddenAction->isChecked());
  s.setValue(QStringLiteral("view/showDots"), m_showDotsAction && m_showDotsAction->isChecked());
  s.setValue(QStringLiteral("view/showGrid"), m_left && m_left->view()->showGrid());
  s.setValue(QStringLiteral("view/flatView"), m_flatViewAction && m_flatViewAction->isChecked());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
  saveSettings();
  QMainWindow::closeEvent(event);
}

void MainWindow::setActivePanel(FilePanel *panel)
{
  if (!panel) return;
  m_active = panel;
  if (m_left)
    m_left->setActive(m_left == panel);
  if (m_right)
    m_right->setActive(m_right == panel);
  m_status->setText(panel->displayPath());
  updateActions();
}

FilePanel *MainWindow::panelById(int id) const
{
  if (m_left && m_left->panelId() == id)
    return m_left;
  if (m_right && m_right->panelId() == id)
    return m_right;
  return nullptr;
}

void MainWindow::focusOtherPanel()
{
  FilePanel *other = otherPanel();
  if (!other)
    return;
  setActivePanel(other);
  other->focusView();
}

bool MainWindow::ensureWritableArchive(const QString &action)
{
  if (!m_active || !m_active->isArchive() || !m_active->isNestedArchive())
    return true;
  QString reason;
  if (m_active->canCommitNestedArchive(&reason))
    return true;
  m_prompts->showError(action + QStringLiteral(" not supported"), reason);
  return false;
}

bool MainWindow::commitNestedArchive(FilePanel *panel)
{
  if (!panel || !panel->isNestedArchive())
    return true;
  const OperationResult result =
      runOperation(QStringLiteral("Saving nested archive"),
                   [panel](ProgressState *state, PromptDispatcher *prompts) {
                     return panel->commitNestedArchive(state, prompts);
                   });
  if (!result.ok)
  {
    if (!result.canceled && !result.error.isEmpty())
      m_prompts->showError(QStringLiteral("Nested archive"), result.error);
    return false;
  }
  QString error;
  if (!panel->refreshNestedParents(nullptr, m_prompts, &error))
  {
    m_prompts->showError(QStringLiteral("Reload archive"), error);
    return false;
  }
  m_status->setText(QStringLiteral("Saved nested archive changes"));
  return true;
}

void MainWindow::updateActions()
{
  const bool archive = m_active && m_active->isArchive();
  if (m_extractAction) m_extractAction->setEnabled(archive);
  if (m_testAction) m_testAction->setEnabled(archive);
  if (m_closeArchiveAction) m_closeArchiveAction->setEnabled(archive);
  if (m_addAction) m_addAction->setEnabled(m_active && !archive);
}

void MainWindow::openArchiveDialog()
{
  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("Open Archive"), QDir::homePath(),
      QStringLiteral("Archives (*.7z *.zip *.rar *.tar *.gz *.tgz *.bz2 *.xz *.cab *.iso *.wim *.arj *.lzh);;All files (*)"));
  if (!path.isEmpty())
    openPath(path);
}

static bool openArchiveSource(const PanelTransfer &transfer, ArchiveSession &session,
                              QVector<int> *rows, ProgressState *state,
                              PromptDispatcher *prompts, QString *error)
{
  if (!session.open(transfer.sourceRoot, QString(), state, prompts, error))
    return false;
  if (!transfer.archivePrefix.isEmpty() && !session.enterPath(transfer.archivePrefix))
  {
    *error = QStringLiteral("Cannot open folder in source archive: %1").arg(transfer.archivePrefix);
    return false;
  }
  QSet<QString> wanted;
  for (const QString &name : transfer.names)
  {
    QString clean = name;
    while (clean.endsWith(QLatin1Char('/')))
      clean.chop(1);
    if (!clean.isEmpty())
      wanted.insert(clean);
  }
  rows->clear();
  for (int row = 0; row < session.itemCount(); ++row)
  {
    QString name = session.item(row).name;
    while (name.endsWith(QLatin1Char('/')))
      name.chop(1);
    if (wanted.contains(name))
      rows->append(row);
  }
  if (rows->isEmpty())
  {
    *error = QStringLiteral("The selected items are no longer present in the source archive.");
    return false;
  }
  return true;
}

void MainWindow::handleTransfer(const PanelTransfer &transfer, const PanelDropTarget &target,
                                Qt::DropAction action)
{
  FilePanel *targetPanel = qobject_cast<FilePanel *>(sender());
  if (!targetPanel)
    targetPanel = m_active;
  if (!targetPanel)
    return;
  setActivePanel(targetPanel);
  performTransfer(transfer, targetPanel, target, action);
}

bool MainWindow::performTransfer(const PanelTransfer &transfer, FilePanel *target,
                                 const PanelDropTarget &targetPath, Qt::DropAction action,
                                 bool confirmMove)
{
  if (!target || !transfer.isValid() || !targetPath.isValid())
    return false;

  const bool move = action == Qt::MoveAction || transfer.cut;
  FilePanel *source = panelById(transfer.sourcePanelId);
  ArchiveSession *sourceSession = nullptr;
  QVector<int> archiveRows = transfer.archiveRows;

  if (transfer.kind == PanelTransfer::Archive)
  {
    if (source && (!source->isArchive() ||
                   source->archiveModel()->session()->archivePath() != transfer.sourceRoot))
      source = nullptr;
    if (!source)
    {
      if (m_left && m_left->isArchive() &&
          m_left->archiveModel()->session()->archivePath() == transfer.sourceRoot)
        source = m_left;
      else if (m_right && m_right->isArchive() &&
               m_right->archiveModel()->session()->archivePath() == transfer.sourceRoot)
        source = m_right;
    }
    if (source && source->isArchive() &&
        source->archiveModel()->session()->archivePath() == transfer.sourceRoot &&
        source->archiveRelativePath() == transfer.archivePrefix)
    {
      sourceSession = source->archiveModel()->session();
      archiveRows.clear();
      const int count = sourceSession->itemCount();
      for (int row : transfer.archiveRows)
      {
        if (row >= 0 && row < count)
          archiveRows.append(row);
      }
      if (archiveRows.isEmpty())
      {
        m_prompts->showError(QStringLiteral("Transfer"),
                             QStringLiteral("No valid source items were selected."));
        return false;
      }
    }
    // Otherwise the source panel was closed or navigated away (typical for
    // clipboard pastes).  The worker operation reopens the archive
    // temporarily when sourceSession is null.
  }
  else
  {
    if (transfer.localPaths.isEmpty())
      return false;
    for (const QString &path : transfer.localPaths)
    {
      if (QFileInfo::exists(path) || QFileInfo(path).isSymLink())
        continue;
      m_prompts->showError(QStringLiteral("Transfer"),
                           QStringLiteral("The source item no longer exists:\n%1").arg(path));
      return false;
    }
  }

  ArchiveSession *targetSession = target->isArchive() ? target->archiveModel()->session() : nullptr;
  if (targetPath.archive)
  {
    if (target->isNestedArchive())
    {
      if (move)
      {
        m_prompts->showInfo(QStringLiteral("Nested archive"),
                            QStringLiteral("Moving items into a nested archive is not supported because "
                                           "the source would be removed before the outer archive is saved.\n\n"
                                           "Copy the items instead (plain drag or Ctrl-drag)."));
        return false;
      }
      QString reason;
      if (!target->canCommitNestedArchive(&reason))
      {
        m_prompts->showError(QStringLiteral("Nested archive"), reason);
        return false;
      }
    }
    if (!targetSession)
    {
      m_prompts->showError(QStringLiteral("Transfer"),
                           QStringLiteral("No archive is open in the destination panel."));
      return false;
    }
    if (transfer.kind == PanelTransfer::Archive &&
        targetSession->archivePath() == transfer.sourceRoot)
    {
      m_prompts->showInfo(QStringLiteral("Transfer"),
                          QStringLiteral("Source and destination are the same archive."));
      return false;
    }
  }
  else
  {
    if (!QFileInfo(targetPath.localDir).isDir())
    {
      m_prompts->showError(QStringLiteral("Transfer"),
                           QStringLiteral("The destination folder does not exist:\n%1")
                               .arg(targetPath.localDir));
      return false;
    }
    const QString sourceDir = transfer.kind == PanelTransfer::Archive
        ? QString() : QFileInfo(transfer.localPaths.first()).absolutePath();
    if (!sourceDir.isEmpty() &&
        QFileInfo(sourceDir).absoluteFilePath() == QFileInfo(targetPath.localDir).absoluteFilePath())
    {
      m_prompts->showInfo(QStringLiteral("Transfer"),
                          QStringLiteral("Source and destination are the same folder."));
      return false;
    }
  }

  QString confirmation;
  if (move)
  {
    if (transfer.kind == PanelTransfer::Archive)
      confirmation = QStringLiteral("The selected archive items will be copied to the destination and then removed from the source archive. Continue?");
    else if (targetPath.archive)
      confirmation = QStringLiteral("The selected files will be added to the archive and then removed from the local filesystem. Continue?");
    else if (confirmMove)
      confirmation = QStringLiteral("Move the selected item(s)? The source is removed after a successful copy.");
  }
  if (!confirmation.isEmpty() &&
      !m_prompts->confirm(QStringLiteral("Move"), confirmation, true))
    return false;

  OperationResult result;
  if (!targetPath.archive && transfer.kind != PanelTransfer::Archive)
  {
    const QStringList paths = transfer.localPaths;
    const QString targetDir = targetPath.localDir;
    result = runOperation(move ? QStringLiteral("Moving") : QStringLiteral("Copying"),
                          [paths, targetDir, move](ProgressState *state, PromptDispatcher *prompts) {
                            return copyLocalItems(paths, targetDir, move, state, prompts);
                          });
    if (result.ok)
    {
      target->refresh();
      selectTransferredItems(target, targetPath, transfer);
    }
  }
  else if (!targetPath.archive && transfer.kind == PanelTransfer::Archive)
  {
    const QString targetDir = targetPath.localDir;
    result = runOperation(move ? QStringLiteral("Moving from archive") : QStringLiteral("Extracting"),
                          [transfer, sourceSession, archiveRows, targetDir, move](ProgressState *state, PromptDispatcher *prompts) {
                            ArchiveSession temporary;
                            ArchiveSession *session = sourceSession;
                            QVector<int> rows = archiveRows;
                            if (!session)
                            {
                              QString openError;
                              if (!openArchiveSource(transfer, temporary, &rows, state, prompts, &openError))
                              {
                                OperationResult failed;
                                failed.error = openError;
                                return failed;
                              }
                              session = &temporary;
                            }
                            ExtractOptions options;
                            options.outputDir = targetDir;
                            options.pathMode = 1;
                            options.overwriteMode = move ? 1 : 0;
                            options.password = session->password();
                            OperationResult extracted = session->extract(rows, options, state, prompts);
                            if (!extracted.ok || !move)
                              return extracted;
                            OperationResult removed = session->deleteItems(rows, state, prompts);
                            removed.files += extracted.files;
                            removed.folders += extracted.folders;
                            removed.unpackedSize += extracted.unpackedSize;
                            removed.packedSize += extracted.packedSize;
                            removed.messages = extracted.messages + removed.messages;
                            return removed;
                          });
    if (result.ok)
    {
      target->refresh();
      selectTransferredItems(target, targetPath, transfer);
    }
  }
  else if (targetPath.archive && transfer.kind != PanelTransfer::Archive)
  {
    const QStringList paths = transfer.localPaths;
    CompressOptions options;
    options.archivePath = targetSession->archivePath();
    options.archiveSubdir = targetPath.archivePrefix;
    options.formatIndex = SevenZipEngine::instance().formatIndex(targetSession->formatName());
    options.password = targetSession->password();
    options.level = 5;
    result = runOperation(move ? QStringLiteral("Moving to archive") : QStringLiteral("Adding to archive"),
                          [paths, options](ProgressState *state, PromptDispatcher *prompts) {
                            return compressFiles(paths, options, state, prompts);
                          });
    if (result.ok && move)
    {
      QStringList failed;
      for (const QString &path : paths)
      {
        if (!removeLocalPath(path))
          failed << path;
      }
      if (!failed.isEmpty())
        m_prompts->showError(QStringLiteral("Remove source"),
                             QStringLiteral("The items were added to the archive, but these sources could not be removed:\n%1")
                                 .arg(failed.join(QLatin1Char('\n'))));
      if (source)
        source->refresh();
    }
  }
  else if (targetPath.archive && transfer.kind == PanelTransfer::Archive)
  {
    const QString targetArchive = targetSession->archivePath();
    const QString targetFormat = targetSession->formatName();
    const QString targetPassword = targetSession->password();
    const QString targetPrefix = targetPath.archivePrefix;
    result = runOperation(move ? QStringLiteral("Moving archive entries")
                               : QStringLiteral("Copying archive entries"),
                          [transfer, sourceSession, archiveRows, targetArchive, targetFormat,
                           targetPassword, targetPrefix, move]
                          (ProgressState *state, PromptDispatcher *prompts) {
                            ArchiveSession temporary;
                            ArchiveSession *session = sourceSession;
                            QVector<int> rows = archiveRows;
                            if (!session)
                            {
                              QString openError;
                              if (!openArchiveSource(transfer, temporary, &rows, state, prompts, &openError))
                              {
                                OperationResult failed;
                                failed.error = openError;
                                return failed;
                              }
                              session = &temporary;
                            }
                            QTemporaryDir temp;
                            if (!temp.isValid())
                            {
                              OperationResult failed;
                              failed.error = QStringLiteral("Cannot create a temporary directory");
                              return failed;
                            }
                            ExtractOptions extractOptions;
                            extractOptions.outputDir = temp.path();
                            extractOptions.pathMode = 1;
                            extractOptions.overwriteMode = 1;
                            extractOptions.password = session->password();
                            OperationResult extracted = session->extract(rows, extractOptions, state, prompts);
                            if (!extracted.ok)
                              return extracted;
                            const QFileInfoList tops = QDir(temp.path()).entryInfoList(
                                QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
                            if (tops.isEmpty())
                            {
                              OperationResult failed;
                              failed.error = QStringLiteral("No files were extracted from the source archive");
                              return failed;
                            }
                            QStringList paths;
                            for (const QFileInfo &info : tops)
                              paths << info.absoluteFilePath();
                            CompressOptions options;
                            options.archivePath = targetArchive;
                            options.archiveSubdir = targetPrefix;
                            options.formatIndex = SevenZipEngine::instance().formatIndex(targetFormat);
                            options.password = targetPassword;
                            options.level = 5;
                            OperationResult added = compressFiles(paths, options, state, prompts);
                            added.messages = extracted.messages + added.messages;
                            added.files += extracted.files;
                            if (!added.ok || !move)
                              return added;
                            OperationResult removed = session->deleteItems(rows, state, prompts);
                            removed.files += added.files;
                            removed.messages = added.messages + removed.messages;
                            return removed;
                          });
  }

  if (!result.ok)
  {
    if (!result.canceled && !result.error.isEmpty())
      m_prompts->showError(QStringLiteral("Transfer failed"), result.error);
    return false;
  }

  bool nestedSaved = true;
  if (targetPath.archive && target->isNestedArchive())
    nestedSaved = commitNestedArchive(target);

  if (targetPath.archive)
  {
    const QString prefix = targetPath.archivePrefix;
    QString error;
    if (!target->reloadArchive(nullptr, m_prompts, &error))
    {
      m_prompts->showError(QStringLiteral("Reload archive"), error);
      return nestedSaved;
    }
    if (target->archiveRelativePath() != prefix && !prefix.isEmpty())
      target->enterArchivePath(prefix);
    if (target->archiveRelativePath() == prefix)
      selectTransferredItems(target, targetPath, transfer);
  }
  if (move && source)
    source->refresh();
  updateActions();
  if (!nestedSaved)
    m_status->setText(QStringLiteral("Nested archive changes could not be saved"));
  else
    m_status->setText(QStringLiteral("%1 %2 item(s)")
                          .arg(move ? QStringLiteral("Moved") : QStringLiteral("Copied"))
                          .arg(result.files ? result.files : transfer.count()));
  return nestedSaved;
}

void MainWindow::selectTransferredItems(FilePanel *target, const PanelDropTarget &targetPath,
                                        const PanelTransfer &transfer)
{
  if (!target)
    return;
  QStringList names;
  for (const QString &name : transfer.names)
  {
    QString clean = name;
    while (clean.endsWith(QLatin1Char('/')))
      clean.chop(1);
    if (!clean.isEmpty())
      names << clean;
  }
  if (names.isEmpty())
    return;
  if (targetPath.archive)
  {
    if (target->archiveRelativePath() != targetPath.archivePrefix)
      return;
    QTimer::singleShot(0, target, [target, names] { target->selectArchiveEntries(names); });
  }
  else
  {
    QStringList paths;
    for (const QString &name : names)
      paths << targetPath.localDir + QLatin1Char('/') + name;
    QTimer::singleShot(0, target, [target, paths] { target->selectLocalPaths(paths); });
  }
}

void MainWindow::openNestedArchive(int row)
{
  FilePanel *panel = qobject_cast<FilePanel *>(sender());
  if (panel)
    setActivePanel(panel);
  else
    panel = m_active;
  if (!panel)
    return;
  ProgressState state;
  QApplication::setOverrideCursor(Qt::WaitCursor);
  QString error;
  const bool opened = panel->openNestedArchive(row, &state, m_prompts, &error);
  QApplication::restoreOverrideCursor();
  if (!opened)
  {
    if (!error.isEmpty())
      m_prompts->showError(QStringLiteral("Open nested archive"), error);
    return;
  }
  m_status->setText(QStringLiteral("Opened %1").arg(panel->displayPath()));
  updateActions();
}

void MainWindow::openPath(const QString &path)
{
  const QFileInfo fi(path);
  if (fi.isDir())
  {
    if (m_active) m_active->setLocalPath(path);
    return;
  }
  if (!fi.exists())
  {
    m_prompts->showError(QStringLiteral("Open"), QStringLiteral("File does not exist:\n%1").arg(path));
    return;
  }
  if (!m_active) return;
  QApplication::setOverrideCursor(Qt::WaitCursor);
  QString error;
  const bool ok = m_active->openArchive(fi.absoluteFilePath(), nullptr, m_prompts, &error);
  QApplication::restoreOverrideCursor();
  if (!ok)
  {
    m_prompts->showError(QStringLiteral("Cannot open archive"), error);
    return;
  }
  m_status->setText(QStringLiteral("Opened %1").arg(fi.fileName()));
  updateActions();
}

void MainWindow::openArchiveRequested(const QString &path)
{
  if (FilePanel *panel = qobject_cast<FilePanel *>(sender()))
    setActivePanel(panel);
  const QFileInfo fi(path);
  if (!fi.exists()) return;
  if (fi.isDir())
  {
    if (m_active) m_active->setLocalPath(path);
    return;
  }
  QApplication::setOverrideCursor(Qt::WaitCursor);
  QString error;
  const bool ok = m_active && m_active->openArchive(fi.absoluteFilePath(), nullptr, m_prompts, &error);
  QApplication::restoreOverrideCursor();
  if (!ok)
  {
    // Not an archive: hand it to the desktop.
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absoluteFilePath()));
    if (m_active) m_active->refresh();
    return;
  }
  updateActions();
}

OperationResult MainWindow::runOperation(const QString &title, const OperationFn &fn)
{
  ProgressState state;
  ProgressDialog dialog(&state, title, this);
  OperationThread thread;
  thread.fn = [this, &fn, &state] { return fn(&state, m_prompts); };
  // PromptDispatcher is substituted below via a second lambda in callers.
  QEventLoop loop;
  connect(&thread, &QThread::finished, &loop, &QEventLoop::quit);
  dialog.show();
  thread.start();
  loop.exec();
  dialog.hide();
  thread.wait();
  if (!thread.result.ok && !thread.result.canceled && !thread.result.error.isEmpty())
  {
    m_status->setText(thread.result.error);
  }
  else if (thread.result.ok)
  {
    m_status->setText(QStringLiteral("Operation finished"));
  }
  else if (thread.result.canceled)
  {
    m_status->setText(QStringLiteral("Operation canceled"));
  }
  return thread.result;
}

void MainWindow::extractArchive()
{
  if (!m_active || !m_active->isArchive())
    return;
  ExtractDialog dialog(this);
  dialog.setArchivePath(m_active->displayPath());
  const QString archiveDir = QFileInfo(m_active->displayPath()).absolutePath();
  dialog.setDefaultOutputDir(archiveDir);
  if (dialog.exec() != QDialog::Accepted)
    return;

  ExtractOptions options = dialog.options();
  const QVector<int> selected = m_active->selectedArchiveRows();
  ArchiveSession *session = m_active->archiveModel()->session();
  const auto result = runOperation(QStringLiteral("Extracting"),
                                   [session, selected, options](ProgressState *state, PromptDispatcher *prompts) {
                                     return session->extract(selected, options, state, prompts);
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(QStringLiteral("Extraction failed"), result.error);
  else if (result.ok)
    m_status->setText(QStringLiteral("Extracted %1 item(s)").arg(result.files));
}

void MainWindow::testArchive()
{
  if (!m_active || !m_active->isArchive())
    return;
  const QVector<int> selected = m_active->selectedArchiveRows();
  ArchiveSession *session = m_active->archiveModel()->session();
  ExtractOptions options;
  options.testMode = true;
  const auto result = runOperation(QStringLiteral("Testing archive"),
                                   [session, selected, options](ProgressState *state, PromptDispatcher *prompts) {
                                     return session->extract(selected, options, state, prompts);
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(QStringLiteral("Test failed"), result.error);
  else if (result.ok)
    m_prompts->showInfo(QStringLiteral("Test completed"), QStringLiteral("No errors were found."));
}

void MainWindow::addToArchive()
{
  if (!m_active || m_active->isArchive())
    return;
  QStringList files = m_active->selectedLocalPaths();
  if (files.isEmpty())
    files << m_active->localPath();
  CompressDialog dialog(files, m_active ? m_active->localPath() : QString(), this);
  if (dialog.exec() != QDialog::Accepted)
    return;
  const CompressOptions options = dialog.options();
  const auto result = runOperation(QStringLiteral("Compressing"),
                                   [files, options](ProgressState *state, PromptDispatcher *prompts) {
                                     return compressFiles(files, options, state, prompts);
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(QStringLiteral("Compression failed"), result.error);
  else if (result.ok)
  {
    m_status->setText(QStringLiteral("Created %1").arg(options.archivePath));
    m_active->refresh();
  }
}

void MainWindow::goUp()
{
  if (m_active && !m_active->goUp())
    m_status->setText(QStringLiteral("Already at the top"));
  else if (m_active)
    m_status->setText(m_active->displayPath());
}

void MainWindow::refreshActive()
{
  if (m_active) m_active->refresh();
}

void MainWindow::closeArchive()
{
  if (m_active)
  {
    m_active->closeArchive();
    m_status->setText(m_active->displayPath());
    updateActions();
  }
}

void MainWindow::selectAll()
{
  if (!m_active) return;
  m_active->selectAllRows();
  m_active->focusView();
}

void MainWindow::calculateHash()
{
  if (!m_active || m_active->isArchive())
    return;
  QStringList files = m_active->selectedLocalPaths();
  if (files.isEmpty())
    files << m_active->localPath();
  const QStringList methods = {QStringLiteral("CRC32"), QStringLiteral("SHA1"), QStringLiteral("SHA256"),
                               QStringLiteral("SHA512"), QStringLiteral("MD5"), QStringLiteral("SHA3-256"),
                               QStringLiteral("BLAKE2sp"), QStringLiteral("XXH64"), QStringLiteral("CRC64")};
  QString method;
  if (!InputDialog::getItem(this, QStringLiteral("Hash"), QStringLiteral("Hash"),
                            QStringLiteral("Hash method:"), methods, 2, &method) ||
      method.isEmpty())
    return;
  HashOptions hopts;
  hopts.methods << method;
  const auto result = runOperation(QStringLiteral("Hashing"),
                                   [files, hopts](ProgressState *state, PromptDispatcher *prompts) {
                                     return hashFiles(files, hopts, state, prompts).operation;
                                   });
  // The richer lines are recomputed by the hash operation; keep a small output dialog.
  if (!result.ok && !result.canceled)
  {
    m_prompts->showError(QStringLiteral("Hashing failed"), result.error);
    return;
  }
  if (result.canceled) return;
  // Re-run is avoided by showing the messages in the status bar; a dedicated result
  // is available in the hash operation and can be expanded later.
  HashResultDialog dialog(QStringLiteral("Hash result"), result.details.isEmpty()
      ? QStringList{QStringLiteral("Hashing completed.")}
      : result.details, this);
  dialog.exec();
}

static bool copyPathRecursive(const QString &src, const QString &dst, QString *error)
{
  const QFileInfo fi(src);
  if (fi.isSymLink())
  {
    QFile::remove(dst);
    if (!QFile::link(fi.symLinkTarget(), dst))
    {
      if (error) *error = QStringLiteral("Cannot create link: %1").arg(dst);
      return false;
    }
    return true;
  }
  if (fi.isDir())
  {
    QDir dir;
    if (!dir.mkpath(dst))
    {
      if (error) *error = QStringLiteral("Cannot create directory: %1").arg(dst);
      return false;
    }
    QDirIterator it(src, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::NoIteratorFlags);
    while (it.hasNext())
    {
      const QString child = it.next();
      if (!copyPathRecursive(child, dst + QLatin1Char('/') + QFileInfo(child).fileName(), error))
        return false;
    }
    return true;
  }
  if (QFile::exists(dst))
  {
    if (error) *error = QStringLiteral("Destination already exists: %1").arg(dst);
    return false;
  }
  if (!QFile::copy(src, dst))
  {
    if (error) *error = QStringLiteral("Cannot copy: %1").arg(src);
    return false;
  }
  return true;
}

static bool removeLocalPath(const QString &path)
{
  const QFileInfo fi(path);
  if (fi.isSymLink() || !fi.isDir())
    return QFile::remove(path);
  return QDir(path).removeRecursively();
}

void MainWindow::copyToOtherPanel() { transferToOtherPanel(false); }
void MainWindow::moveToOtherPanel() { transferToOtherPanel(true); }

void MainWindow::transferToOtherPanel(bool move)
{
  FilePanel *source = m_active;
  FilePanel *target = otherPanel();
  if (!source || !target)
    return;
  const PanelTransfer transfer = source->selectionTransfer();
  if (!transfer.isValid())
  {
    m_status->setText(QStringLiteral("Nothing selected"));
    return;
  }
  PanelDropTarget dropTarget;
  if (target->isArchive())
  {
    dropTarget.archive = true;
    dropTarget.archivePrefix = target->archiveRelativePath();
  }
  else
  {
    dropTarget.localDir = target->localPath();
  }
  performTransfer(transfer, target, dropTarget, move ? Qt::MoveAction : Qt::CopyAction, true);
}

void MainWindow::setClipboardFromSelection(bool cut)
{
  if (!m_active)
    return;
  PanelTransfer transfer = m_active->selectionTransfer();
  if (!transfer.isValid())
  {
    m_status->setText(QStringLiteral("Nothing selected"));
    return;
  }
  transfer.cut = cut;

  auto *mime = new QMimeData;
  mime->setData(QString::fromLatin1(kPanelTransferMime), transfer.encode());
  mime->setData(QString::fromLatin1(kLegacyCutMime),
                cut ? QByteArrayLiteral("1") : QByteArrayLiteral("0"));
  mime->setText(transfer.names.join(QLatin1Char('\n')));
  if (transfer.kind == PanelTransfer::Local)
  {
    QList<QUrl> urls;
    for (const QString &path : transfer.localPaths)
      urls << QUrl::fromLocalFile(path);
    mime->setUrls(urls);
    QByteArray gnome = cut ? QByteArrayLiteral("cut\n") : QByteArrayLiteral("copy\n");
    for (const QUrl &url : urls)
      gnome += url.toEncoded() + '\n';
    mime->setData(QStringLiteral("x-special/gnome-copied-files"), gnome);
    mime->setData(QStringLiteral("application/x-kde-cutselection"),
                  cut ? QByteArrayLiteral("1") : QByteArrayLiteral("0"));
  }
  QApplication::clipboard()->setMimeData(mime);
  m_status->setText(QStringLiteral("%1 %2 item(s)")
                        .arg(cut ? QStringLiteral("Cut") : QStringLiteral("Copied"))
                        .arg(transfer.count()));
}

void MainWindow::clipboardCopy()
{
  setClipboardFromSelection(false);
}

void MainWindow::clipboardCut()
{
  setClipboardFromSelection(true);
}

void MainWindow::clipboardPaste()
{
  if (!m_active)
    return;
  const QMimeData *mime = QApplication::clipboard()->mimeData();
  if (!mime)
    return;

  PanelTransfer transfer;
  if (mime->hasFormat(QString::fromLatin1(kPanelTransferMime)))
    transfer = PanelTransfer::decode(mime->data(QString::fromLatin1(kPanelTransferMime)));
  if (!transfer.isValid() && mime->hasUrls())
  {
    QStringList paths;
    for (const QUrl &url : mime->urls())
      if (url.isLocalFile())
        paths << url.toLocalFile();
    transfer = PanelTransfer::fromUrls(paths, panelTransferIsCut(mime));
  }
  if (!transfer.isValid())
    return;

  PanelDropTarget target;
  if (m_active->isArchive())
  {
    target.archive = true;
    target.archivePrefix = m_active->archiveRelativePath();
  }
  else
  {
    target.localDir = m_active->localPath();
  }

  const bool cut = transfer.cut || panelTransferIsCut(mime);
  if (performTransfer(transfer, m_active, target,
                      cut ? Qt::MoveAction : Qt::CopyAction))
  {
    if (cut)
      QApplication::clipboard()->clear();
  }
}

void MainWindow::splitFile()
{
  if (!m_active || m_active->isArchive()) return;
  const QStringList selected = m_active->selectedLocalPaths();
  if (selected.size() != 1 || QFileInfo(selected.first()).isDir())
  {
    m_prompts->showInfo(QStringLiteral("Split File"), QStringLiteral("Select one file to split."));
    return;
  }
  const QString src = selected.first();
  int mib = 1;
  if (!InputDialog::getInt(this, QStringLiteral("Split File"), QStringLiteral("Split File"),
                           QStringLiteral("Volume size (MiB):"), 1, 1024 * 64, 1, &mib))
    return;
  QFile in(src);
  if (!in.open(QIODevice::ReadOnly))
  {
    m_prompts->showError(QStringLiteral("Split File"), QStringLiteral("Cannot open the source file."));
    return;
  }
  const qint64 chunk = qint64(mib) * 1024 * 1024;
  int part = 1;
  QStringList created;
  QApplication::setOverrideCursor(Qt::WaitCursor);
  while (!in.atEnd())
  {
    const QByteArray data = in.read(chunk);
    if (data.isEmpty()) break;
    const QString name = QStringLiteral("%1.%2").arg(src).arg(part, 3, 10, QLatin1Char('0'));
    QFile out(name);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size())
    {
      created << name;
      QApplication::restoreOverrideCursor();
      m_prompts->showError(QStringLiteral("Split File"), QStringLiteral("Cannot write volume: %1").arg(name));
      return;
    }
    created << name;
    ++part;
  }
  QApplication::restoreOverrideCursor();
  m_status->setText(QStringLiteral("Created %1 volume(s)").arg(created.size()));
  m_active->refresh();
}

void MainWindow::combineFiles()
{
  if (!m_active || m_active->isArchive()) return;
  const QStringList selected = m_active->selectedLocalPaths();
  if (selected.size() != 1 || !selected.first().endsWith(QStringLiteral(".001")))
  {
    m_prompts->showInfo(QStringLiteral("Combine Files"), QStringLiteral("Select the first volume (.001) to combine."));
    return;
  }
  const QString first = selected.first();
  const QString base = first.left(first.size() - 4);
  const QString outPath = QFileDialog::getSaveFileName(this, QStringLiteral("Combine to"), base);
  if (outPath.isEmpty()) return;
  QFile out(outPath);
  if (!out.open(QIODevice::WriteOnly))
  {
    m_prompts->showError(QStringLiteral("Combine Files"), QStringLiteral("Cannot create the output file."));
    return;
  }
  QApplication::setOverrideCursor(Qt::WaitCursor);
  for (int part = 1; ; ++part)
  {
    const QString name = QStringLiteral("%1.%2").arg(base).arg(part, 3, 10, QLatin1Char('0'));
    QFile in(name);
    if (!in.exists()) break;
    if (!in.open(QIODevice::ReadOnly))
    {
      QApplication::restoreOverrideCursor();
      m_prompts->showError(QStringLiteral("Combine Files"), QStringLiteral("Cannot read volume: %1").arg(name));
      return;
    }
    while (!in.atEnd())
    {
      const QByteArray data = in.read(1 << 20);
      if (data.isEmpty()) break;
      if (out.write(data) != data.size())
      {
        QApplication::restoreOverrideCursor();
        m_prompts->showError(QStringLiteral("Combine Files"), QStringLiteral("Cannot write the output file."));
        return;
      }
    }
  }
  QApplication::restoreOverrideCursor();
  m_status->setText(QStringLiteral("Combined to %1").arg(outPath));
  m_active->refresh();
}

void MainWindow::renameSelected()
{
  if (!m_active) return;
  if (m_active->isArchive())
  {
    if (!ensureWritableArchive(QStringLiteral("Rename")))
      return;
    const QVector<int> rows = m_active->selectedArchiveRows();
    if (rows.size() != 1)
    {
      m_prompts->showInfo(QStringLiteral("Rename"), QStringLiteral("Select one item to rename."));
      return;
    }
    const ArchiveEntryInfo &entry = m_active->archiveModel()->entry(rows.first());
    QString newName = entry.name;
    if (!InputDialog::getText(this, QStringLiteral("Rename"), QStringLiteral("Rename"),
                              QStringLiteral("New name:"), &newName) ||
        newName.isEmpty() || newName == entry.name)
      return;
    ArchiveSession *session = m_active->archiveModel()->session();
    const auto result = runOperation(QStringLiteral("Renaming item"),
                                     [session, row = rows.first(), newName](ProgressState *state, PromptDispatcher *prompts) {
                                       return session->renameItem(row, newName, state, prompts);
                                     });
    if (!result.ok && !result.canceled)
      m_prompts->showError(QStringLiteral("Rename failed"), result.error);
    if (result.ok && m_active->isNestedArchive())
      commitNestedArchive(m_active);
    m_active->archiveModel()->refresh();
    updateActions();
    return;
  }
  const QStringList selected = m_active->selectedLocalPaths();
  if (selected.size() != 1)
  {
    m_prompts->showInfo(QStringLiteral("Rename"), QStringLiteral("Select one item to rename."));
    return;
  }
  const QFileInfo fi(selected.first());
  QString newName = fi.fileName();
  if (!InputDialog::getText(this, QStringLiteral("Rename"), QStringLiteral("Rename"),
                            QStringLiteral("New name:"), &newName) ||
      newName.isEmpty() || newName == fi.fileName())
    return;
  const QString dest = fi.absolutePath() + QLatin1Char('/') + newName;
  const bool renamed = fi.isDir() ? QDir().rename(fi.absoluteFilePath(), dest)
                                  : QFile::rename(fi.absoluteFilePath(), dest);
  if (!renamed)
    m_prompts->showError(QStringLiteral("Rename failed"), QStringLiteral("Cannot rename:\n%1").arg(fi.absoluteFilePath()));
  m_active->refresh();
}

void MainWindow::deleteSelected()
{
  if (!m_active) return;
  if (m_active->isArchive())
  {
    if (!ensureWritableArchive(QStringLiteral("Delete")))
      return;
    const QVector<int> rows = m_active->selectedArchiveRows();
    if (rows.isEmpty()) return;
    if (!m_prompts->confirm(QStringLiteral("Delete from archive"),
                            QStringLiteral("Delete %1 selected item(s) from the archive?\nThe archive will be rewritten.")
                                .arg(rows.size()), true))
      return;
    ArchiveSession *session = m_active->archiveModel()->session();
    const auto result = runOperation(QStringLiteral("Deleting items"),
                                     [session, rows](ProgressState *state, PromptDispatcher *prompts) {
                                       return session->deleteItems(rows, state, prompts);
                                     });
    if (!result.ok && !result.canceled)
      m_prompts->showError(QStringLiteral("Delete failed"), result.error);
    if (result.ok && m_active->isNestedArchive())
      commitNestedArchive(m_active);
    m_active->archiveModel()->refresh();
    updateActions();
    return;
  }
  const QStringList selected = m_active->selectedLocalPaths();
  if (selected.isEmpty()) return;
  if (!m_prompts->confirm(QStringLiteral("Delete"),
                          QStringLiteral("Delete %1 selected item(s)?\n\nThey will be moved to the trash when possible.").arg(selected.size()),
                          true))
    return;
  QStringList failed;
  for (const QString &path : selected)
  {
    bool ok = QFile::moveToTrash(path);
    if (!ok)
    {
      ok = removeLocalPath(path);
    }
    if (!ok) failed << path;
  }
  if (!failed.isEmpty())
    m_prompts->showError(QStringLiteral("Delete failed"), failed.join(QLatin1Char('\n')));
  m_active->refresh();
}

void MainWindow::copySelected() { 
  if (!m_active || m_active->isArchive()) return;
  const QStringList selected = m_active->selectedLocalPaths();
  if (selected.isEmpty()) return;
  const QString dest = QFileDialog::getExistingDirectory(this, QStringLiteral("Copy to folder"), m_active->localPath());
  if (dest.isEmpty()) return;
  QStringList failed;
  for (const QString &path : selected)
  {
    QString error;
    const QString target = dest + QLatin1Char('/') + QFileInfo(path).fileName();
    if (!copyPathRecursive(path, target, &error)) failed << error;
  }
  if (!failed.isEmpty()) m_prompts->showError(QStringLiteral("Copy failed"), failed.join(QLatin1Char('\n')));
  m_active->refresh();
}

void MainWindow::moveSelected()
{
  if (!m_active || m_active->isArchive()) return;
  const QStringList selected = m_active->selectedLocalPaths();
  if (selected.isEmpty()) return;
  const QString dest = QFileDialog::getExistingDirectory(this, QStringLiteral("Move to folder"), m_active->localPath());
  if (dest.isEmpty()) return;
  QStringList failed;
  for (const QString &path : selected)
  {
    const QString target = dest + QLatin1Char('/') + QFileInfo(path).fileName();
    if (QFileInfo::exists(target)) { failed << QStringLiteral("Destination already exists: %1").arg(target); continue; }
    bool ok = QFile::rename(path, target);
    if (!ok)
    {
      QString error;
      ok = copyPathRecursive(path, target, &error);
      if (ok)
      {
        ok = removeLocalPath(path);
      }
      if (!ok) failed << (error.isEmpty() ? path : error);
    }
  }
  if (!failed.isEmpty()) m_prompts->showError(QStringLiteral("Move failed"), failed.join(QLatin1Char('\n')));
  m_active->refresh();
}

void MainWindow::createFolder()
{
  if (!m_active) return;
  if (m_active->isArchive())
  {
    if (!ensureWritableArchive(QStringLiteral("Create folder")))
      return;
    QString name = QStringLiteral("New Folder");
    if (!InputDialog::getText(this, QStringLiteral("Create Folder"), QStringLiteral("Create Folder"),
                              QStringLiteral("Folder name:"), &name) || name.isEmpty())
      return;
    ArchiveSession *session = m_active->archiveModel()->session();
    const auto result = runOperation(QStringLiteral("Creating folder"),
                                     [session, name](ProgressState *state, PromptDispatcher *prompts) {
                                       return session->createFolder(name, state, prompts);
                                     });
    if (!result.ok && !result.canceled)
      m_prompts->showError(QStringLiteral("Create Folder"), result.error);
    if (result.ok && m_active->isNestedArchive())
      commitNestedArchive(m_active);
    m_active->archiveModel()->refresh();
    updateActions();
    return;
  }
  QString name = QStringLiteral("New Folder");
  if (!InputDialog::getText(this, QStringLiteral("Create Folder"), QStringLiteral("Create Folder"),
                            QStringLiteral("Folder name:"), &name) || name.isEmpty())
    return;
  if (!QDir(m_active->localPath()).mkdir(name))
    m_prompts->showError(QStringLiteral("Create Folder"), QStringLiteral("Cannot create folder."));
  m_active->refresh();
}

QString commandSetting(const char *key, const char *fallback)
{
  QSettings settings;
  const QString value = settings.value(QString::fromLatin1(key), QString::fromLatin1(fallback)).toString().trimmed();
  return value.isEmpty() ? QString::fromLatin1(fallback) : value;
}

void MainWindow::launchExternal(const QString &command, const QString &path)
{
  QStringList parts = QProcess::splitCommand(command);
  if (parts.isEmpty())
  {
    m_prompts->showError(QStringLiteral("External tool"), QStringLiteral("The configured command is empty."));
    return;
  }
  const QString program = parts.takeFirst();
  parts << path;
  if (!QProcess::startDetached(program, parts))
    m_prompts->showError(QStringLiteral("External tool"),
                         QStringLiteral("Cannot start %1.").arg(program));
}

bool MainWindow::materializeItem(QString *path, QString *error)
{
  if (!m_active) return false;
  if (!m_active->isArchive())
  {
    QStringList selected = m_active->selectedLocalPaths();
    if (selected.isEmpty())
    {
      const int row = m_active->currentRow();
      if (row >= 0) selected << m_active->localModel()->filePath(row);
    }
    if (selected.size() != 1) return false;
    *path = selected.first();
    return true;
  }

  const int row = m_active->currentRow();
  if (row < 0) return false;
  const ArchiveEntryInfo entry = m_active->archiveModel()->entry(row);
  if (entry.isDir) return false;
  auto temp = std::make_shared<QTemporaryDir>();
  if (!temp->isValid()) return false;
  ExtractOptions eo;
  eo.outputDir = temp->path();
  eo.pathMode = 2;
  eo.overwriteMode = 1;
  eo.password = m_active->archiveModel()->session()->password();
  ProgressState progress;
  const auto result = m_active->archiveModel()->session()->extract({row}, eo, &progress, m_prompts);
  if (!result.ok)
  {
    if (error) *error = result.error;
    return false;
  }
  QDirIterator it(temp->path(), QDir::Files, QDirIterator::Subdirectories);
  if (!it.hasNext())
  {
    if (error) *error = QStringLiteral("The item could not be extracted");
    return false;
  }
  *path = it.next();
  m_externalTemps.append(temp);
  return true;
}

void MainWindow::openExternalView()
{
  QString path;
  QString error;
  if (!materializeItem(&path, &error))
  {
    if (!error.isEmpty()) m_prompts->showError(QStringLiteral("View"), error);
    return;
  }
  launchExternal(commandSetting("tools/viewerCommand", "xdg-open"), path);
}

void MainWindow::openExternalEdit()
{
  QString path;
  QString error;
  if (!materializeItem(&path, &error))
  {
    if (!error.isEmpty()) m_prompts->showError(QStringLiteral("Edit"), error);
    return;
  }
  launchExternal(commandSetting("tools/editorCommand", "xdg-open"), path);
}

void MainWindow::diffSelected()
{
  if (!m_active) return;
  QStringList paths;
  if (!m_active->isArchive())
  {
    paths = m_active->selectedLocalPaths();
    if (paths.size() != 2)
    {
      m_prompts->showInfo(QStringLiteral("Diff"), QStringLiteral("Select exactly two files to compare."));
      return;
    }
  }
  else
  {
    const QVector<int> rows = m_active->selectedArchiveRows();
    if (rows.size() != 2)
    {
      m_prompts->showInfo(QStringLiteral("Diff"), QStringLiteral("Select exactly two archive items to compare."));
      return;
    }
    QStringList failed;
    for (int row : rows)
    {
      auto temp = std::make_shared<QTemporaryDir>();
      ExtractOptions eo;
      eo.outputDir = temp->path();
      eo.pathMode = 2;
      eo.overwriteMode = 1;
      eo.password = m_active->archiveModel()->session()->password();
      ProgressState progress;
      const auto result = m_active->archiveModel()->session()->extract({row}, eo, &progress, m_prompts);
      if (!result.ok)
      {
        failed << result.error;
        continue;
      }
      QDirIterator it(temp->path(), QDir::Files, QDirIterator::Subdirectories);
      if (!it.hasNext())
      {
        failed << QStringLiteral("Item could not be extracted");
        continue;
      }
      paths << it.next();
      m_externalTemps.append(temp);
    }
    if (paths.size() != 2)
    {
      m_prompts->showError(QStringLiteral("Diff"), failed.join(QLatin1Char('\n')));
      return;
    }
  }

  const QString command = commandSetting("tools/diffCommand", "diff -u");
  QStringList parts = QProcess::splitCommand(command);
  if (parts.isEmpty())
  {
    m_prompts->showError(QStringLiteral("Diff"), QStringLiteral("The diff command is empty."));
    return;
  }
  const QString program = parts.takeFirst();
  parts << paths;
  QProcess process;
  process.start(program, parts);
  if (!process.waitForStarted(3000))
  {
    m_prompts->showError(QStringLiteral("Diff"), QStringLiteral("Cannot start %1.").arg(program));
    return;
  }
  if (!process.waitForFinished(15000))
  {
    process.kill();
    process.waitForFinished(1000);
    m_prompts->showError(QStringLiteral("Diff"), QStringLiteral("The diff command timed out."));
    return;
  }
  const QString output = QString::fromLocal8Bit(process.readAllStandardOutput())
                         + QString::fromLocal8Bit(process.readAllStandardError());
  HashResultDialog dialog(QStringLiteral("Diff"), output.isEmpty()
      ? QStringList{QStringLiteral("No differences.")}
      : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts), this);
  dialog.exec();
}

void MainWindow::editComment()
{
  if (!m_active || !m_active->isArchive())
  {
    m_prompts->showInfo(QStringLiteral("Comment"), QStringLiteral("Comments are supported for archive items (ZIP)."));
    return;
  }
  if (!ensureWritableArchive(QStringLiteral("Comment")))
    return;
  const int row = m_active->currentRow();
  if (row < 0) return;
  const ArchiveEntryInfo &entry = m_active->archiveModel()->entry(row);
  QString comment = entry.comment;
  if (!InputDialog::getMultiLineText(this, QStringLiteral("Comment"), QStringLiteral("Comment"),
                                     QStringLiteral("Comment for %1:").arg(entry.name), &comment))
    return;
  ArchiveSession *session = m_active->archiveModel()->session();
  const auto result = runOperation(QStringLiteral("Saving comment"),
                                   [session, row, comment](ProgressState *state, PromptDispatcher *prompts) {
                                     return session->setComment(row, comment, state, prompts);
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(QStringLiteral("Comment failed"), result.error);
  else if (result.ok)
  {
    if (m_active->isNestedArchive())
      commitNestedArchive(m_active);
    m_active->archiveModel()->refresh();
    m_status->setText(QStringLiteral("Comment saved"));
  }
}

void MainWindow::createLink()
{
  if (!m_active || m_active->isArchive()) return;
  const QStringList selected = m_active->selectedLocalPaths();
  if (selected.size() != 1)
  {
    m_prompts->showInfo(QStringLiteral("Create Link"), QStringLiteral("Select one item to link to."));
    return;
  }
  const QFileInfo source(selected.first());
  const QStringList types = {QStringLiteral("Symbolic link"), QStringLiteral("Hard link")};
  QString type;
  if (!InputDialog::getItem(this, QStringLiteral("Create Link"), QStringLiteral("Create Link"),
                            QStringLiteral("Link type:"), types, 0, &type))
    return;
  QString linkPath = source.absolutePath() + QLatin1Char('/') + source.fileName() + QStringLiteral(" link");
  if (!InputDialog::getText(this, QStringLiteral("Create Link"), QStringLiteral("Create Link"),
                            QStringLiteral("Link path:"), &linkPath) ||
      linkPath.isEmpty())
    return;
  if (QFileInfo::exists(linkPath))
  {
    m_prompts->showError(QStringLiteral("Create Link"), QStringLiteral("The link path already exists:\n%1").arg(linkPath));
    return;
  }
  bool created = false;
  if (type.startsWith(QStringLiteral("Symbolic")))
    created = QFile::link(source.absoluteFilePath(), linkPath);
  else
  {
    const QByteArray src = QFile::encodeName(source.absoluteFilePath());
    const QByteArray dst = QFile::encodeName(linkPath);
    created = (::link(src.constData(), dst.constData()) == 0);
  }
  if (!created)
    m_prompts->showError(QStringLiteral("Create Link"), QStringLiteral("Cannot create the link.\nThe target may be on another filesystem."));
  m_active->refresh();
}

void MainWindow::createFile()
{
  if (!m_active || m_active->isArchive()) return;
  QString name = QStringLiteral("New File.txt");
  if (!InputDialog::getText(this, QStringLiteral("Create File"), QStringLiteral("Create File"),
                            QStringLiteral("File name:"), &name) || name.isEmpty())
    return;
  QFile f(m_active->localPath() + QLatin1Char('/') + name);
  if (!f.open(QIODevice::WriteOnly))
    m_prompts->showError(QStringLiteral("Create File"), QStringLiteral("Cannot create file."));
  m_active->refresh();
}

void MainWindow::showProperties()
{
  if (!m_active) return;
  if (m_active->isArchive())
  {
    const int row = m_active->currentRow();
    if (row >= 0)
    {
      const ArchiveEntryInfo &e = m_active->archiveModel()->entry(row);
      m_prompts->showInfo(QStringLiteral("Properties"),
                          QStringLiteral("Name: %1\nSize: %2\nModified: %3\nMethod: %4\nEncrypted: %5")
                              .arg(e.name)
                              .arg(e.size)
                              .arg(QLocale().toString(e.modified, QLocale::ShortFormat))
                              .arg(e.method)
                              .arg(e.isEncrypted ? QStringLiteral("yes") : QStringLiteral("no")));
    }
    return;
  }
  const QStringList selected = m_active->selectedLocalPaths();
  if (selected.size() != 1) return;
  const QFileInfo fi(selected.first());
  m_prompts->showInfo(QStringLiteral("Properties"),
                      QStringLiteral("Name: %1\nPath: %2\nType: %3\nSize: %4\nModified: %5\nPermissions: %6")
                          .arg(fi.fileName(), fi.absoluteFilePath(),
                               fi.isDir() ? QStringLiteral("Folder") : QStringLiteral("File"))
                          .arg(QLocale().formattedDataSize(fi.size(), 1, QLocale::DataSizeTraditionalFormat))
                          .arg(QLocale().toString(fi.lastModified(), QLocale::ShortFormat))
                          .arg(QString::number(int(fi.permissions()), 16)));
}

void MainWindow::addFavorite()
{
  if (!m_active) return;
  QString path = m_active->localPath();
  if (path.isEmpty()) return;
  QSettings s;
  QStringList favs = s.value(QStringLiteral("favorites"), QStringList()).toStringList();
  if (!favs.contains(path))
  {
    favs.append(path);
    s.setValue(QStringLiteral("favorites"), favs);
  }
  m_status->setText(QStringLiteral("Added to favorites: %1").arg(path));
}

void MainWindow::clearFavorites()
{
  QSettings s;
  s.setValue(QStringLiteral("favorites"), QStringList());
  m_status->setText(QStringLiteral("Favorites cleared"));
}

void MainWindow::showOptions()
{
  QSettings settings;
  const QString viewer = settings.value(QStringLiteral("tools/viewerCommand"), QStringLiteral("xdg-open")).toString();
  const QString editor = settings.value(QStringLiteral("tools/editorCommand"), QStringLiteral("xdg-open")).toString();
  const QString diff = settings.value(QStringLiteral("tools/diffCommand"), QStringLiteral("diff -u")).toString();
  const QString language = settings.value(QStringLiteral("ui/language"), QStringLiteral("system")).toString();
  OptionsDialog dialog(m_showHiddenAction && m_showHiddenAction->isChecked(),
                       m_showDotsAction && m_showDotsAction->isChecked(),
                       m_left && m_left->view()->showGrid(),
                       m_flatViewAction && m_flatViewAction->isChecked(),
                       viewer, editor, diff, language, this);
  if (dialog.exec() != QDialog::Accepted) return;
  if (m_showHiddenAction) m_showHiddenAction->setChecked(dialog.showHidden());
  if (m_showDotsAction) m_showDotsAction->setChecked(dialog.showDots());
  if (m_flatViewAction) m_flatViewAction->setChecked(dialog.flatView());
  if (m_left) m_left->setShowGrid(dialog.showGrid());
  if (m_right) m_right->setShowGrid(dialog.showGrid());
  settings.setValue(QStringLiteral("view/showGrid"), dialog.showGrid());
  settings.setValue(QStringLiteral("view/flatView"), dialog.flatView());
  settings.setValue(QStringLiteral("tools/viewerCommand"), dialog.viewerCommand().isEmpty() ? QStringLiteral("xdg-open") : dialog.viewerCommand());
  settings.setValue(QStringLiteral("tools/editorCommand"), dialog.editorCommand().isEmpty() ? QStringLiteral("xdg-open") : dialog.editorCommand());
  settings.setValue(QStringLiteral("tools/diffCommand"), dialog.diffCommand().isEmpty() ? QStringLiteral("diff -u") : dialog.diffCommand());
  settings.setValue(QStringLiteral("ui/language"), dialog.language());
}

void MainWindow::showBenchmark()
{
  BenchmarkDialog dialog(this);
  connect(&dialog, &BenchmarkDialog::runRequested, this, [this, &dialog] {
    const BenchmarkOptions options = dialog.options();
    const auto result = runOperation(QStringLiteral("Benchmark"),
                                     [options](ProgressState *state, PromptDispatcher *prompts) {
                                       return runBenchmark(options, state, prompts).operation;
                                     });
    dialog.setLog(result.details);
    dialog.setRunning(false);
    if (!result.ok && !result.canceled)
      m_prompts->showError(QStringLiteral("Benchmark failed"), result.error);
  });
  dialog.exec();
}

void MainWindow::showRoot()
{
  if (m_active) m_active->setRootMode();
  updateActions();
}

QStringList MainWindow::selectedLocalArchiveFiles() const
{
  QStringList result;
  if (!m_active || m_active->isArchive())
    return result;
  QStringList candidates = m_active->selectedLocalPaths();
  if (candidates.isEmpty())
  {
    const int row = m_active->currentRow();
    if (row >= 0)
      candidates << m_active->localModel()->filePath(row);
  }
  static const QStringList exts = {
      QStringLiteral("7z"), QStringLiteral("zip"), QStringLiteral("rar"),
      QStringLiteral("tar"), QStringLiteral("gz"), QStringLiteral("tgz"),
      QStringLiteral("bz2"), QStringLiteral("xz"), QStringLiteral("cab"),
      QStringLiteral("iso"), QStringLiteral("wim"), QStringLiteral("arj"),
      QStringLiteral("lzh"), QStringLiteral("zst"), QStringLiteral("lzma"),
      QStringLiteral("z")};
  for (const QString &path : candidates)
  {
    const QFileInfo fi(path);
    if (fi.isFile() && exts.contains(fi.suffix().toLower()))
      result << fi.absoluteFilePath();
  }
  return result;
}

void MainWindow::openCurrentItem()
{
  if (!m_active) return;
  if (m_active->isArchive())
  {
    const int row = m_active->currentRow();
    if (row < 0) return;
    const ArchiveEntryInfo entry = m_active->archiveModel()->entry(row);
    if (entry.isDir)
    {
      if (m_active->archiveModel()->enter(row))
      {
        m_status->setText(m_active->displayPath());
        updateActions();
      }
      return;
    }
    const QStringList exts = {QStringLiteral("7z"), QStringLiteral("zip"), QStringLiteral("rar"),
                              QStringLiteral("tar"), QStringLiteral("gz"), QStringLiteral("tgz"),
                              QStringLiteral("bz2"), QStringLiteral("xz"), QStringLiteral("cab"),
                              QStringLiteral("iso"), QStringLiteral("wim"), QStringLiteral("arj"),
                              QStringLiteral("lzh"), QStringLiteral("zst")};
    if (exts.contains(QFileInfo(entry.name).suffix().toLower()))
      openNestedArchive(row);
    else
      openExternalView();
    return;
  }
  QStringList selected = m_active->selectedLocalPaths();
  if (selected.isEmpty())
  {
    const int row = m_active->currentRow();
    if (row >= 0) selected << m_active->localModel()->filePath(row);
  }
  if (selected.isEmpty()) return;
  const QFileInfo fi(selected.first());
  if (fi.isDir())
  {
    m_active->setLocalPath(fi.absoluteFilePath());
    return;
  }
  if (isArchiveFile(fi.absoluteFilePath()))
    openPath(fi.absoluteFilePath());
  else
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absoluteFilePath()));
}

void MainWindow::openInside(const QString &formatHint)
{
  if (!m_active) return;
  if (m_active->isArchive())
  {
    openCurrentItem();
    return;
  }
  QStringList selected = m_active->selectedLocalPaths();
  if (selected.isEmpty())
  {
    const int row = m_active->currentRow();
    if (row >= 0) selected << m_active->localModel()->filePath(row);
  }
  if (selected.isEmpty()) return;
  const QFileInfo fi(selected.first());
  if (!fi.isFile()) return;
  QApplication::setOverrideCursor(Qt::WaitCursor);
  QString error;
  const bool ok = m_active->openArchive(fi.absoluteFilePath(), nullptr, m_prompts, &error,
                                        QString(), formatHint);
  QApplication::restoreOverrideCursor();
  if (!ok)
  {
    m_prompts->showError(QStringLiteral("Open Inside"), error);
    return;
  }
  m_status->setText(QStringLiteral("Opened %1").arg(fi.fileName()));
  updateActions();
}

void MainWindow::openCurrentArchiveInside(const QString &formatHint)
{
  if (!m_active || !m_active->isArchive()) return;
  const int row = m_active->currentRow();
  if (row < 0) return;
  const ArchiveEntryInfo entry = m_active->archiveModel()->entry(row);
  if (entry.isDir)
  {
    if (m_active->archiveModel()->enter(row)) updateActions();
    return;
  }
  ProgressState state;
  QApplication::setOverrideCursor(Qt::WaitCursor);
  QString error;
  const bool opened = m_active->openNestedArchive(row, &state, m_prompts, &error, formatHint);
  QApplication::restoreOverrideCursor();
  if (!opened)
  {
    if (!error.isEmpty())
      m_prompts->showError(QStringLiteral("Open Inside"), error);
    return;
  }
  m_status->setText(QStringLiteral("Opened %1").arg(m_active->displayPath()));
  updateActions();
}

void MainWindow::openOutside()
{
  if (!m_active) return;
  QString path;
  QString error;
  if (materializeItem(&path, &error))
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
  else if (!error.isEmpty())
    m_prompts->showError(QStringLiteral("Open Outside"), error);
}

void MainWindow::extractHere()
{
  if (!m_active) return;
  if (!m_active->isArchive())
  {
    extractLocalArchives(false);
    return;
  }
  ArchiveSession *session = m_active->archiveModel()->session();
  if (!session) return;
  const QVector<int> rows = m_active->selectedArchiveRows();
  ExtractOptions options;
  options.outputDir = QFileInfo(session->archivePath()).absolutePath();
  options.pathMode = 0;
  options.overwriteMode = 0;
  options.password = session->password();
  const auto result = runOperation(QStringLiteral("Extracting here"),
                                   [session, rows, options](ProgressState *state, PromptDispatcher *prompts) {
                                     return session->extract(rows, options, state, prompts);
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(QStringLiteral("Extraction failed"), result.error);
  else if (result.ok)
    m_status->setText(QStringLiteral("Extracted %1 item(s) to the archive folder").arg(result.files));
}

void MainWindow::extractLocalArchives(bool chooseDirectory)
{
  if (!m_active || m_active->isArchive()) return;
  const QStringList archives = selectedLocalArchiveFiles();
  if (archives.isEmpty())
  {
    m_prompts->showInfo(QStringLiteral("Extract"), QStringLiteral("Select one or more archive files first."));
    return;
  }
  QString outputDir = QFileInfo(archives.first()).absolutePath();
  if (chooseDirectory)
    outputDir = QFileDialog::getExistingDirectory(this, QStringLiteral("Extract archives to"), outputDir);
  if (outputDir.isEmpty()) return;
  const auto result = runOperation(QStringLiteral("Extracting archives"),
                                   [archives, outputDir](ProgressState *state, PromptDispatcher *prompts) {
                                     OperationResult total;
                                     total.ok = true;
                                     for (const QString &path : archives)
                                     {
                                       ArchiveSession session;
                                       QString openError;
                                       if (!session.open(path, QString(), state, prompts, &openError))
                                       {
                                         total.ok = false;
                                         total.error = openError;
                                         continue;
                                       }
                                       ExtractOptions eo;
                                       eo.outputDir = outputDir;
                                       eo.pathMode = 0;
                                       eo.overwriteMode = 0;
                                       eo.password = session.password();
                                       auto result = session.extract({}, eo, state, prompts);
                                       total.files += result.files;
                                       total.folders += result.folders;
                                       total.unpackedSize += result.unpackedSize;
                                       total.messages += result.messages;
                                       if (!result.ok)
                                       {
                                         total.ok = false;
                                         total.error = result.error;
                                       }
                                       session.close();
                                     }
                                     return total;
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(QStringLiteral("Extraction failed"), result.error);
  else if (result.ok)
    m_status->setText(QStringLiteral("Extracted %1 item(s)").arg(result.files));
  m_active->refresh();
}

void MainWindow::testLocalArchives()
{
  if (!m_active || m_active->isArchive()) return;
  const QStringList archives = selectedLocalArchiveFiles();
  if (archives.isEmpty())
  {
    m_prompts->showInfo(QStringLiteral("Test archive"), QStringLiteral("Select one or more archive files first."));
    return;
  }
  const auto result = runOperation(QStringLiteral("Testing archives"),
                                   [archives](ProgressState *state, PromptDispatcher *prompts) {
                                     OperationResult total;
                                     total.ok = true;
                                     for (const QString &path : archives)
                                     {
                                       ArchiveSession session;
                                       QString openError;
                                       if (!session.open(path, QString(), state, prompts, &openError))
                                       {
                                         total.ok = false;
                                         total.error = openError;
                                         continue;
                                       }
                                       ExtractOptions eo;
                                       eo.testMode = true;
                                       eo.password = session.password();
                                       auto result = session.extract({}, eo, state, prompts);
                                       total.files += result.files;
                                       total.unpackedSize += result.unpackedSize;
                                       total.messages += result.messages;
                                       if (!result.ok)
                                       {
                                         total.ok = false;
                                         total.error = result.error;
                                       }
                                       session.close();
                                     }
                                     return total;
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(QStringLiteral("Test failed"), result.error);
  else if (result.ok)
    m_prompts->showInfo(QStringLiteral("Test completed"), QStringLiteral("No errors were found."));
}

void MainWindow::compressSelectionQuick(const QString &extension)
{
  if (!m_active || m_active->isArchive()) return;
  QStringList files = m_active->selectedLocalPaths();
  QString targetDir = m_active->localPath();
  if (files.isEmpty())
  {
    const QFileInfo current(targetDir);
    files << current.absoluteFilePath();
    targetDir = current.absolutePath();
    if (targetDir.isEmpty()) targetDir = current.absoluteFilePath();
  }
  const QFileInfo first(files.first());
  QString base = first.isDir() ? first.fileName() : first.completeBaseName();
  if (base.isEmpty()) base = QStringLiteral("archive");
  const QString archivePath = targetDir + QLatin1Char('/') + base + QLatin1Char('.') + extension;
  for (const QString &input : files)
  {
    if (QFileInfo(input).absoluteFilePath() == QFileInfo(archivePath).absoluteFilePath())
    {
      m_prompts->showError(QStringLiteral("Compression failed"),
                           QStringLiteral("The output archive cannot be one of the input files."));
      return;
    }
  }
  if (QFileInfo::exists(archivePath) &&
      !m_prompts->confirm(QStringLiteral("Replace archive"),
                          QStringLiteral("%1 already exists. Replace it?").arg(archivePath), true))
    return;
  CompressOptions options;
  options.archivePath = archivePath;
  options.formatIndex = SevenZipEngine::instance().formatIndex(extension == QLatin1String("zip")
      ? QStringLiteral("zip") : QStringLiteral("7z"));
  options.level = 5;
  const auto result = runOperation(QStringLiteral("Compressing"),
                                   [files, options](ProgressState *state, PromptDispatcher *prompts) {
                                     return compressFiles(files, options, state, prompts);
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(QStringLiteral("Compression failed"), result.error);
  else if (result.ok)
  {
    m_status->setText(QStringLiteral("Created %1").arg(archivePath));
    m_active->refresh();
  }
}

void MainWindow::hashSelection(const QString &method)
{
  if (!m_active) return;
  HashOptions options;
  options.methods << method;
  if (!m_active->isArchive())
  {
    QStringList files = m_active->selectedLocalPaths();
    if (files.isEmpty())
    {
      const int row = m_active->currentRow();
      if (row >= 0) files << m_active->localModel()->filePath(row);
      else files << m_active->localPath();
    }
    const auto result = runOperation(QStringLiteral("Hashing"),
                                     [files, options](ProgressState *state, PromptDispatcher *prompts) {
                                       return hashFiles(files, options, state, prompts).operation;
                                     });
    if (!result.ok && !result.canceled)
      m_prompts->showError(QStringLiteral("Hashing failed"), result.error);
    else if (result.ok)
    {
      HashResultDialog dialog(QStringLiteral("%1 hash").arg(method), result.details, this);
      dialog.exec();
    }
    return;
  }

  ArchiveSession *session = m_active->archiveModel()->session();
  if (!session) return;
  const QVector<int> rows = m_active->selectedArchiveRows();
  const auto result = runOperation(QStringLiteral("Hashing archive items"),
                                   [session, rows, options](ProgressState *state, PromptDispatcher *prompts) {
                                     QTemporaryDir temp;
                                     if (!temp.isValid())
                                     {
                                       OperationResult r;
                                       r.error = QStringLiteral("Cannot create a temporary directory");
                                       return r;
                                     }
                                     ExtractOptions eo;
                                     eo.outputDir = temp.path();
                                     eo.pathMode = 1;
                                     eo.overwriteMode = 1;
                                     eo.password = session->password();
                                     auto extracted = session->extract(rows, eo, state, prompts);
                                     if (!extracted.ok) return extracted;
                                     QStringList files;
                                     QDirIterator it(temp.path(), QDir::Files, QDirIterator::Subdirectories);
                                     while (it.hasNext()) files << it.next();
                                     if (files.isEmpty())
                                     {
                                       OperationResult r;
                                       r.error = QStringLiteral("No files to hash");
                                       return r;
                                     }
                                     auto hashed = hashFiles(files, options, state, prompts);
                                     return hashed.operation;
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(QStringLiteral("Hashing failed"), result.error);
  else if (result.ok)
  {
    HashResultDialog dialog(QStringLiteral("%1 hash").arg(method), result.details, this);
    dialog.exec();
  }
}

void MainWindow::showPanelContextMenu(const QPoint &globalPos)
{
  FilePanel *panel = qobject_cast<FilePanel *>(sender());
  if (panel)
    setActivePanel(panel);
  else
    panel = m_active;
  if (!panel) return;

  const bool archive = panel->isArchive();
  const int selectedCount = archive ? panel->selectedArchiveRows().size()
                                    : panel->selectedLocalPaths().size();
  QMenu menu(this);

  auto add = [&menu, this](const QString &text, const std::function<void()> &fn,
                           bool enabled = true, const QKeySequence &shortcut = QKeySequence()) {
    QAction *action = menu.addAction(text, this, fn);
    action->setEnabled(enabled);
    if (!shortcut.isEmpty())
      action->setShortcut(shortcut);
    return action;
  };

  if (!archive)
  {
    const QStringList archiveFiles = selectedLocalArchiveFiles();
    const bool hasArchive = !archiveFiles.isEmpty();
    QMenu *seven = menu.addMenu(QStringLiteral("7-Zip"));
    seven->addAction(QStringLiteral("Open archive"), this, [this] { openCurrentItem(); })->setEnabled(hasArchive);
    seven->addAction(QStringLiteral("Extract files..."), this, [this] { extractLocalArchives(true); })->setEnabled(hasArchive);
    seven->addAction(QStringLiteral("Extract Here"), this, [this] { extractLocalArchives(false); })->setEnabled(hasArchive);
    seven->addAction(QStringLiteral("Test archive"), this, [this] { testLocalArchives(); })->setEnabled(hasArchive);
    seven->addSeparator();
    seven->addAction(QStringLiteral("Add to archive..."), this, &MainWindow::addToArchive);
    seven->addAction(QStringLiteral("Add to \"<name>.7z\""), this, [this] { compressSelectionQuick(QStringLiteral("7z")); });
    seven->addAction(QStringLiteral("Add to \"<name>.zip\""), this, [this] { compressSelectionQuick(QStringLiteral("zip")); });
    menu.addSeparator();

    add(QStringLiteral("Open"), [this] { openCurrentItem(); }, selectedCount == 1, QKeySequence(Qt::Key_Return));
    add(QStringLiteral("Open Inside"), [this] { openInside(QString()); }, selectedCount == 1, QKeySequence(QStringLiteral("Ctrl+PgDown")));
    add(QStringLiteral("Open Inside *"), [this] { openInside(QStringLiteral("*")); }, selectedCount == 1);
    add(QStringLiteral("Open Inside #"), [this] { openInside(QStringLiteral("#")); }, selectedCount == 1);
    add(QStringLiteral("Open Outside"), [this] { openOutside(); }, selectedCount == 1, QKeySequence(QStringLiteral("Shift+Return")));
    add(QStringLiteral("View"), [this] { openExternalView(); }, selectedCount == 1, QKeySequence(Qt::Key_F3));
    add(QStringLiteral("Edit"), [this] { openExternalEdit(); }, selectedCount == 1, QKeySequence(Qt::Key_F4));
    menu.addSeparator();
    add(QStringLiteral("Rename..."), [this] { renameSelected(); }, selectedCount == 1, QKeySequence(Qt::Key_F2));
    add(QStringLiteral("Copy to Other Panel"), [this] { copyToOtherPanel(); }, selectedCount >= 1, QKeySequence(Qt::Key_F5));
    add(QStringLiteral("Move to Other Panel"), [this] { moveToOtherPanel(); }, selectedCount >= 1, QKeySequence(Qt::Key_F6));
    add(QStringLiteral("Copy To..."), [this] { copySelected(); }, selectedCount >= 1, QKeySequence(QStringLiteral("Ctrl+F5")));
    add(QStringLiteral("Move To..."), [this] { moveSelected(); }, selectedCount >= 1, QKeySequence(QStringLiteral("Ctrl+F6")));
    add(QStringLiteral("Delete"), [this] { deleteSelected(); }, selectedCount >= 1, QKeySequence(Qt::Key_Delete));
    menu.addSeparator();
    add(QStringLiteral("Split file..."), [this] { splitFile(); }, selectedCount == 1);
    add(QStringLiteral("Combine files..."), [this] { combineFiles(); }, selectedCount == 1);
    add(QStringLiteral("Properties"), [this] { showProperties(); }, selectedCount == 1, QKeySequence(QStringLiteral("Alt+Return")));
    add(QStringLiteral("Comment..."), [this] { editComment(); }, false, QKeySequence(QStringLiteral("Ctrl+Z")));
    QMenu *crc = menu.addMenu(QStringLiteral("CRC"));
    for (const QString &method : {QStringLiteral("CRC32"), QStringLiteral("CRC64"), QStringLiteral("MD5"),
                                  QStringLiteral("SHA1"), QStringLiteral("SHA256"), QStringLiteral("SHA512"),
                                  QStringLiteral("SHA3-256"), QStringLiteral("BLAKE2sp"), QStringLiteral("XXH64")})
      crc->addAction(method, this, [this, method] { hashSelection(method); });
    menu.addSeparator();
    add(QStringLiteral("Create Folder..."), [this] { createFolder(); }, true, QKeySequence(Qt::Key_F7));
    add(QStringLiteral("Create File..."), [this] { createFile(); }, true, QKeySequence(QStringLiteral("Ctrl+N")));
    menu.addSeparator();
    add(QStringLiteral("Link..."), [this] { createLink(); }, selectedCount == 1);
    add(QStringLiteral("Alternate streams"), [] {}, false);
    menu.addSeparator();
    add(QStringLiteral("Select All"), [panel] { panel->selectAllRows(); }, true, QKeySequence(QKeySequence::SelectAll));
    add(QStringLiteral("Invert Selection"), [panel] { panel->invertSelection(); });
    add(QStringLiteral("Refresh"), [panel] { panel->refresh(); }, true, QKeySequence(QKeySequence::Refresh));
  }
  else
  {
    add(QStringLiteral("Open"), [this] { openCurrentItem(); }, selectedCount == 1, QKeySequence(Qt::Key_Return));
    add(QStringLiteral("Open Inside"), [this] { openCurrentItem(); }, selectedCount == 1, QKeySequence(QStringLiteral("Ctrl+PgDown")));
    add(QStringLiteral("Open Inside *"), [this] { openCurrentArchiveInside(QStringLiteral("*")); }, selectedCount == 1);
    add(QStringLiteral("Open Inside #"), [this] { openCurrentArchiveInside(QStringLiteral("#")); }, selectedCount == 1);
    add(QStringLiteral("Open Outside"), [this] { openOutside(); }, selectedCount == 1, QKeySequence(QStringLiteral("Shift+Return")));
    add(QStringLiteral("View"), [this] { openExternalView(); }, selectedCount == 1, QKeySequence(Qt::Key_F3));
    add(QStringLiteral("Edit"), [this] { openExternalEdit(); }, selectedCount == 1, QKeySequence(Qt::Key_F4));
    menu.addSeparator();
    add(QStringLiteral("Extract files..."), [this] { extractArchive(); }, true, QKeySequence(QStringLiteral("Ctrl+E")));
    add(QStringLiteral("Extract Here"), [this] { extractHere(); }, true);
    add(QStringLiteral("Test archive"), [this] { testArchive(); }, true, QKeySequence(QStringLiteral("Ctrl+T")));
    menu.addSeparator();
    add(QStringLiteral("Rename..."), [this] { renameSelected(); }, selectedCount == 1, QKeySequence(Qt::Key_F2));
    add(QStringLiteral("Copy to Other Panel"), [this] { copyToOtherPanel(); }, selectedCount >= 1, QKeySequence(Qt::Key_F5));
    add(QStringLiteral("Move to Other Panel"), [this] { moveToOtherPanel(); }, selectedCount >= 1, QKeySequence(Qt::Key_F6));
    add(QStringLiteral("Delete"), [this] { deleteSelected(); }, selectedCount >= 1, QKeySequence(Qt::Key_Delete));
    add(QStringLiteral("Comment..."), [this] { editComment(); }, selectedCount == 1, QKeySequence(QStringLiteral("Ctrl+Z")));
    add(QStringLiteral("Properties"), [this] { showProperties(); }, selectedCount == 1, QKeySequence(QStringLiteral("Alt+Return")));
    QMenu *crc = menu.addMenu(QStringLiteral("CRC"));
    for (const QString &method : {QStringLiteral("CRC32"), QStringLiteral("CRC64"), QStringLiteral("MD5"),
                                  QStringLiteral("SHA1"), QStringLiteral("SHA256"), QStringLiteral("SHA512"),
                                  QStringLiteral("SHA3-256"), QStringLiteral("BLAKE2sp"), QStringLiteral("XXH64")})
      crc->addAction(method, this, [this, method] { hashSelection(method); });
    menu.addSeparator();
    add(QStringLiteral("Create Folder..."), [this] { createFolder(); }, true, QKeySequence(Qt::Key_F7));
    add(QStringLiteral("Select All"), [panel] { panel->selectAllRows(); }, true, QKeySequence(QKeySequence::SelectAll));
    add(QStringLiteral("Invert Selection"), [panel] { panel->invertSelection(); });
    add(QStringLiteral("Refresh"), [panel] { panel->refresh(); }, true, QKeySequence(QKeySequence::Refresh));
  }

  menu.setStyleSheet(QStringLiteral(
      "QMenu { padding: 4px; }"
      "QMenu::item { padding: 3px 24px 3px 10px; }"
      "QMenu::separator { margin: 2px 8px; }"));
  menu.exec(globalPos);
}

void MainWindow::showAbout()
{
  AboutDialog dialog(this);
  dialog.exec();
}
