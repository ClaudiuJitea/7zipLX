#include "MainWindow.h"
#include "ui/widgets/FilePanel.h"
#include "ui/dialogs/ExtractDialog.h"
#include "ui/dialogs/CompressDialog.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/dialogs/HashResultDialog.h"
#include "ui/dialogs/OptionsDialog.h"
#include "ui/dialogs/AboutDialog.h"
#include "ui/dialogs/BenchmarkDialog.h"
#include "backend/ArchiveTypes.h"
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
#include "ui/Theme.h"
#include "DesktopIntegration.h"
#include "ExternalToolController.h"
#include "OperationRunner.h"
#include "HashController.h"
#include "TransferController.h"

#include <QAction>
#include <QKeyEvent>
#include <QLineEdit>
#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
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
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTableView>
#include <QThread>
#include <QTimer>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

static bool removeLocalPath(const QString &path);

namespace {

// QToolButton left-aligns its icon and text; this one centers them so a row
// of equal-width toolbar buttons reads evenly.
class CenteredToolButton : public QToolButton
{
public:
  using QToolButton::QToolButton;

protected:
  void paintEvent(QPaintEvent *) override
  {
    QStylePainter painter(this);
    QStyleOptionToolButton opt;
    initStyleOption(&opt);
    QStyleOptionToolButton panel = opt;
    panel.text.clear();
    panel.icon = QIcon();
    painter.drawComplexControl(QStyle::CC_ToolButton, panel);
    // The label is laid out inside the stylesheet's padding + border
    // (10px + 1px a side, see Theme.cpp's QToolBar#mainToolbar QToolButton).
    constexpr int kInset = 11;
    const int content = opt.iconSize.width() + 4 + opt.fontMetrics.horizontalAdvance(opt.text);
    const int natural = qMin(width(), content + 2 * kInset);
    opt.rect = QRect((width() - natural) / 2, 0, natural, height());
    painter.drawControl(QStyle::CE_ToolButtonLabel, opt);
  }
};

} // namespace

// Returns why a single file or folder name cannot be used, or an empty string.
static QString invalidItemName(const QString &name)
{
  if (name.trimmed().isEmpty())
    return MainWindow::tr("The name cannot be empty.");
  if (name == QLatin1String(".") || name == QLatin1String(".."))
    return MainWindow::tr("\"%1\" is a reserved name.").arg(name);
  if (name.contains(QLatin1Char('/')) || name.contains(QChar(0)))
    return MainWindow::tr("A name cannot contain \"/\".");
  return QString();
}

static bool isArchiveFile(const QString &path)
{
  return isArchiveFileName(path);
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
  setWindowTitle(QStringLiteral("7zipLX"));
  setMinimumSize(900, 560);

  m_prompts = new PromptDispatcher(this);
  m_runner = std::make_unique<OperationRunner>(this, m_prompts);
  m_externalTools = std::make_unique<ExternalToolController>(this, m_runner.get(), m_prompts);
  m_hash = std::make_unique<HashController>(this, m_runner.get(), m_prompts);
  connect(m_externalTools.get(), &ExternalToolController::statusMessage, this,
          [this](const QString &text) { m_status->setText(text); });
  connect(m_externalTools.get(), &ExternalToolController::pendingEditsChanged, this,
          [this](int count) {
            if (m_updateEditsAction)
              m_updateEditsAction->setEnabled(count > 0);
          });

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

  m_status = new QLabel(MainWindow::tr("Ready"), this);
  m_status->setObjectName(QStringLiteral("windowStatus"));
  statusBar()->addWidget(m_status, 1);
  statusBar()->setSizeGripEnabled(false);

  auto *fileMenu = menuBar()->addMenu(MainWindow::tr("&File"));
  m_openAction = fileMenu->addAction(MainWindow::tr("&Open Archive..."), this, &MainWindow::openArchiveDialog);
  m_openAction->setShortcut(QKeySequence::Open);
  fileMenu->addSeparator();
  m_addAction = fileMenu->addAction(MainWindow::tr("&Add to Archive..."), this, &MainWindow::addToArchive);
  m_addAction->setShortcut(QKeySequence(QStringLiteral("Alt+A")));
  m_extractAction = fileMenu->addAction(MainWindow::tr("&Extract..."), this, &MainWindow::extractArchive);
  m_extractAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
  m_testAction = fileMenu->addAction(MainWindow::tr("&Test Archive"), this, &MainWindow::testArchive);
  m_testAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
  m_closeArchiveAction = fileMenu->addAction(MainWindow::tr("&Close Archive"), this, &MainWindow::closeArchive);
  m_closeArchiveAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));
  fileMenu->addSeparator();
  fileMenu->addAction(MainWindow::tr("E&xit"), this, &QWidget::close)->setShortcut(QKeySequence::Quit);

  auto *editMenu = menuBar()->addMenu(MainWindow::tr("&Edit"));
  editMenu->addAction(MainWindow::tr("Select &All"), this, &MainWindow::selectAll)->setShortcut(QKeySequence::SelectAll);
  editMenu->addSeparator();
  editMenu->addAction(MainWindow::tr("Cu&t"), this, &MainWindow::clipboardCut)->setShortcut(QKeySequence::Cut);
  editMenu->addAction(MainWindow::tr("&Copy"), this, &MainWindow::clipboardCopy)->setShortcut(QKeySequence::Copy);
  editMenu->addAction(MainWindow::tr("&Paste"), this, &MainWindow::clipboardPaste)->setShortcut(QKeySequence::Paste);
  editMenu->addSeparator();
  editMenu->addAction(MainWindow::tr("&Refresh"), this, &MainWindow::refreshActive)->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
  editMenu->addSeparator();
  editMenu->addAction(MainWindow::tr("&Rename..."), this, &MainWindow::renameSelected)->setShortcut(QKeySequence(Qt::Key_F2));
  editMenu->addAction(MainWindow::tr("&Copy to Other Panel"), this, &MainWindow::copyToOtherPanel)->setShortcut(QKeySequence(Qt::Key_F5));
  editMenu->addAction(MainWindow::tr("&Move to Other Panel"), this, &MainWindow::moveToOtherPanel)->setShortcut(QKeySequence(Qt::Key_F6));
  editMenu->addAction(MainWindow::tr("Copy &To..."), this, &MainWindow::copySelected)->setShortcut(QKeySequence(QStringLiteral("Ctrl+F5")));
  editMenu->addAction(MainWindow::tr("Move T&o..."), this, &MainWindow::moveSelected)->setShortcut(QKeySequence(QStringLiteral("Ctrl+F6")));
  editMenu->addAction(MainWindow::tr("&Delete"), this, &MainWindow::deleteSelected)->setShortcut(QKeySequence(Qt::Key_Delete));
  editMenu->addSeparator();
  editMenu->addAction(MainWindow::tr("Create &Folder..."), this, &MainWindow::createFolder)->setShortcut(QKeySequence(Qt::Key_F7));
  editMenu->addAction(MainWindow::tr("Create &File..."), this, &MainWindow::createFile)->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
  editMenu->addAction(MainWindow::tr("P&roperties"), this, &MainWindow::showProperties)->setShortcut(QKeySequence(QStringLiteral("Alt+Return")));
  fileMenu->addSeparator();
  fileMenu->addAction(MainWindow::tr("Create &Link..."), this, &MainWindow::createLink);
  fileMenu->addAction(MainWindow::tr("&View"), this, &MainWindow::openExternalView)->setShortcut(QKeySequence(Qt::Key_F3));
  fileMenu->addAction(MainWindow::tr("&Edit"), this, &MainWindow::openExternalEdit)->setShortcut(QKeySequence(Qt::Key_F4));
  fileMenu->addAction(MainWindow::tr("Di&ff"), this, &MainWindow::diffSelected);
  fileMenu->addAction(MainWindow::tr("&Comment..."), this, &MainWindow::editComment)->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
  m_updateEditsAction = fileMenu->addAction(MainWindow::tr("&Update Archive from Edited File(s)"),
                                            this, [this] { m_externalTools->saveAllPending(); });
  m_updateEditsAction->setEnabled(false);
  fileMenu->addAction(MainWindow::tr("&Split File..."), this, &MainWindow::splitFile);
  fileMenu->addAction(MainWindow::tr("&Combine Files..."), this, &MainWindow::combineFiles);

  auto *viewMenu = menuBar()->addMenu(MainWindow::tr("&View"));
  m_twoPanelsAction = viewMenu->addAction(MainWindow::tr("&Two Panels"));
  m_twoPanelsAction->setCheckable(true);
  m_twoPanelsAction->setShortcut(QKeySequence(Qt::Key_F9));
  connect(m_twoPanelsAction, &QAction::toggled, this, &MainWindow::setTwoPanels);
  viewMenu->addSeparator();
  viewMenu->addAction(MainWindow::tr("&Up One Level"), this, &MainWindow::goUp)->setShortcut(QKeySequence(QStringLiteral("Alt+Up")));
  viewMenu->addAction(MainWindow::tr("&Computer"), this, &MainWindow::showRoot);
  viewMenu->addSeparator();
  m_showHiddenAction = viewMenu->addAction(MainWindow::tr("Show &Hidden Files"));
  m_showHiddenAction->setCheckable(true);
  m_showDotsAction = viewMenu->addAction(MainWindow::tr("Show &Parent Entry (..)"));
  m_showDotsAction->setCheckable(true);
  m_flatViewAction = viewMenu->addAction(MainWindow::tr("&Flat View"));
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

  auto *favMenu = menuBar()->addMenu(MainWindow::tr("&Favorites"));
  favMenu->addAction(MainWindow::tr("&Add Current Folder"), this, &MainWindow::addFavorite);
  favMenu->addSeparator();
  connect(favMenu, &QMenu::aboutToShow, this, [this, favMenu] {
    while (!favMenu->actions().isEmpty())
      delete favMenu->actions().first();
    favMenu->addAction(MainWindow::tr("&Add Current Folder"), this, &MainWindow::addFavorite);
    favMenu->addAction(MainWindow::tr("&Clear Favorites"), this, &MainWindow::clearFavorites);
    favMenu->addSeparator();
    QSettings s;
    const QStringList favs = s.value(QStringLiteral("favorites"), QStringList()).toStringList();
    for (const QString &path : favs)
      favMenu->addAction(path, this, [this, path] { if (m_active) m_active->setLocalPath(path); });
  });

  auto *toolsMenu = menuBar()->addMenu(MainWindow::tr("&Tools"));
  toolsMenu->addAction(MainWindow::tr("&Benchmark..."), this, &MainWindow::showBenchmark);
  toolsMenu->addAction(MainWindow::tr("&Hash..."), this, &MainWindow::calculateHash)->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));
  toolsMenu->addSeparator();
  toolsMenu->addAction(MainWindow::tr("&Options..."), this, &MainWindow::showOptions);

  auto *helpMenu = menuBar()->addMenu(MainWindow::tr("&Help"));
  helpMenu->addAction(MainWindow::tr("&About 7zipLX..."), this, &MainWindow::showAbout);

  auto *toolbar = addToolBar(MainWindow::tr("Main"));
  toolbar->setObjectName(QStringLiteral("mainToolbar"));
  toolbar->setMovable(false);
  toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  toolbar->setIconSize(QSize(18, 18));

  m_addAction->setIcon(Theme::toolbarIcon(QStringLiteral("add.svg")));
  m_extractAction->setIcon(Theme::toolbarIcon(QStringLiteral("extract.svg")));
  m_testAction->setIcon(Theme::toolbarIcon(QStringLiteral("test.svg")));

  auto *logo = new QLabel(toolbar);
  logo->setObjectName(QStringLiteral("brandLogo"));
  logo->setPixmap(QIcon(QStringLiteral(":/icons/7ziplx-48.png")).pixmap(QSize(22, 22)));
  toolbar->addWidget(logo);
  auto *brand = new QLabel(QStringLiteral("7zipLX"), toolbar);
  brand->setObjectName(QStringLiteral("brand"));
  toolbar->addWidget(brand);
  toolbar->addSeparator();
  m_addAction->setText(MainWindow::tr("Add"));
  m_addAction->setToolTip(MainWindow::tr("Add the selection to an archive (Alt+A)"));
  m_extractAction->setText(MainWindow::tr("Extract"));
  m_extractAction->setToolTip(MainWindow::tr("Extract from the open archive (Ctrl+E)"));
  m_testAction->setText(MainWindow::tr("Test"));
  m_testAction->setToolTip(MainWindow::tr("Check the open archive for errors (Ctrl+T)"));
  m_openAction->setText(MainWindow::tr("Open"));
  m_openAction->setToolTip(MainWindow::tr("Open an archive (Ctrl+O)"));
  QList<QToolButton *> actionButtons;
  auto addActionButton = [&](QAction *action) {
    auto *button = new CenteredToolButton(toolbar);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setIconSize(toolbar->iconSize());
    button->setFocusPolicy(Qt::NoFocus);
    button->setDefaultAction(action);
    toolbar->addWidget(button);
    actionButtons.append(button);
  };
  addActionButton(m_addAction);
  addActionButton(m_extractAction);
  addActionButton(m_testAction);
  toolbar->addSeparator();
  addActionButton(m_openAction);
  int buttonWidth = 0;
  for (QToolButton *button : std::as_const(actionButtons))
    buttonWidth = qMax(buttonWidth, button->sizeHint().width());
  for (QToolButton *button : std::as_const(actionButtons))
    button->setFixedWidth(buttonWidth);
  // Kept for the menus and shortcuts; every panel has its own Up/Refresh.
  m_upAction = new QAction(MainWindow::tr("Up"), this);
  connect(m_upAction, &QAction::triggered, this, &MainWindow::goUp);
  m_refreshAction = new QAction(MainWindow::tr("Refresh"), this);
  connect(m_refreshAction, &QAction::triggered, this, &MainWindow::refreshActive);

  auto *spacer = new QWidget(toolbar);
  spacer->setObjectName(QStringLiteral("toolbarSpacer"));
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  spacer->setAttribute(Qt::WA_TransparentForMouseEvents);
  toolbar->addWidget(spacer);
  m_filter = new QLineEdit(toolbar);
  m_filter->setObjectName(QStringLiteral("toolbarFilter"));
  m_filter->setPlaceholderText(MainWindow::tr("Filter this folder  (Ctrl+F)"));
  m_filter->setClearButtonEnabled(true);
  m_filter->setFixedWidth(250);
  m_filter->installEventFilter(this);
  toolbar->addWidget(m_filter);
  connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
    if (m_active)
      m_active->setNameFilter(text);
  });
  auto *findShortcut = new QAction(this);
  findShortcut->setShortcut(QKeySequence::Find);
  connect(findShortcut, &QAction::triggered, this, [this] {
    m_filter->setFocus(Qt::ShortcutFocusReason);
    m_filter->selectAll();
  });
  addAction(findShortcut);
  m_twoPanelsAction->setToolTip(MainWindow::tr("Show a second panel (F9)"));
  toolbar->addAction(m_twoPanelsAction);

  for (FilePanel *panel : {m_left, m_right})
  {
    connect(panel, &FilePanel::nameFilterChanged, this, [this, panel](const QString &text) {
      if (m_active == panel && m_filter && m_filter->text() != text)
      {
        QSignalBlocker block(m_filter);
        m_filter->setText(text);
      }
    });
  }
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
  auto dragMaterializer = [this](FilePanel *panel, const PanelTransfer &transfer, QString *error) {
    return m_externalTools->materializeForDrag(panel, transfer, error);
  };
  m_left->setDragMaterializer(dragMaterializer);
  m_right->setDragMaterializer(dragMaterializer);
  auto dragReleaser = [this](FilePanel *, const QStringList &paths) {
    m_externalTools->releaseDrag(paths);
  };
  m_left->setDragReleaser(dragReleaser);
  m_right->setDragReleaser(dragReleaser);
  connect(m_left, &FilePanel::pathChanged, this, [this](const QString &) {
    if (m_active == m_left)
    {
      updateWindowTitle();
      updateActions(); // entering or leaving an archive changes what applies
    }
  });
  connect(m_right, &FilePanel::pathChanged, this, [this](const QString &) {
    if (m_active == m_right)
    {
      updateWindowTitle();
      updateActions(); // entering or leaving an archive changes what applies
    }
  });

  TransferContext transferContext;
  transferContext.prompts = m_prompts;
  transferContext.panelById = [this](int id) { return panelById(id); };
  transferContext.leftPanel = [this] { return m_left; };
  transferContext.rightPanel = [this] { return m_right; };
  transferContext.run = [this](const QString &title,
                               const std::function<OperationResult(ProgressState *, PromptDispatcher *)> &fn) {
    return runOperation(title, fn);
  };
  transferContext.commitNested = [this](FilePanel *panel) { return commitNestedArchive(panel); };
  transferContext.setStatus = [this](const QString &text) { m_status->setText(text); };
  transferContext.updateActions = [this] { updateActions(); };
  m_transfer = std::make_unique<TransferController>(transferContext, this);

  updateThemeIcons();
  setActivePanel(m_left);
  setTwoPanels(false);
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
  const bool twoPanels = s.value(QStringLiteral("window/twoPanels"), false).toBool();
  setTwoPanels(twoPanels);
}

void MainWindow::setTwoPanels(bool show)
{
  m_twoPanels = show;
  if (m_right)
    m_right->setVisible(show);
  if (m_twoPanelsAction && m_twoPanelsAction->isChecked() != show)
  {
    QSignalBlocker blocker(m_twoPanelsAction);
    m_twoPanelsAction->setChecked(show);
  }
  if (!show && m_active == m_right)
    setActivePanel(m_left);
}

void MainWindow::saveSettings()
{
  QSettings s;
  s.setValue(QStringLiteral("window/geometry"), saveGeometry());
  s.setValue(QStringLiteral("window/splitter"), m_splitter->saveState());
  s.setValue(QStringLiteral("window/leftPath"), m_left->localPath());
  s.setValue(QStringLiteral("window/rightPath"), m_right->localPath());
  s.setValue(QStringLiteral("window/twoPanels"), m_twoPanels);
  s.setValue(QStringLiteral("view/showHidden"), m_showHiddenAction && m_showHiddenAction->isChecked());
  s.setValue(QStringLiteral("view/showDots"), m_showDotsAction && m_showDotsAction->isChecked());
  s.setValue(QStringLiteral("view/showGrid"), m_left && m_left->view()->showGrid());
  s.setValue(QStringLiteral("view/flatView"), m_flatViewAction && m_flatViewAction->isChecked());
}

void MainWindow::changeEvent(QEvent *event)
{
  if (event->type() == QEvent::PaletteChange)
    updateThemeIcons();
  QMainWindow::changeEvent(event);
}

void MainWindow::updateThemeIcons()
{
  if (m_openAction)
    m_openAction->setIcon(Theme::toolbarIcon(QStringLiteral("open.svg")));
  if (m_addAction)
    m_addAction->setIcon(Theme::toolbarIcon(QStringLiteral("add.svg")));
  if (m_extractAction)
    m_extractAction->setIcon(Theme::toolbarIcon(QStringLiteral("extract.svg")));
  if (m_testAction)
    m_testAction->setIcon(Theme::toolbarIcon(QStringLiteral("test.svg")));
  if (m_upAction)
    m_upAction->setIcon(Theme::toolbarIcon(QStringLiteral("up.svg")));
  if (m_refreshAction)
    m_refreshAction->setIcon(Theme::toolbarIcon(QStringLiteral("refresh.svg")));
  if (m_left)
    m_left->updateThemeIcons();
  if (m_right)
    m_right->updateThemeIcons();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
  if (m_externalTools && m_externalTools->hasPendingEdits())
  {
    if (!m_prompts->confirm(MainWindow::tr("Unsaved archive edits"),
                            MainWindow::tr("Some files edited in external applications have not "
                                           "been written back to their archives.\n\n"
                                           "Close and discard those changes?"),
                            true))
    {
      event->ignore();
      return;
    }
    m_externalTools->discardPending();
  }
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
  if (m_filter && m_filter->text() != panel->nameFilter())
  {
    QSignalBlocker block(m_filter);
    m_filter->setText(panel->nameFilter());
  }
  updateWindowTitle();
  updateActions();
}

void MainWindow::updateWindowTitle()
{
  if (!m_active)
    return;
  QString where = m_active->displayPath();
  while (where.size() > 1 && where.endsWith(QLatin1Char('/')))
    where.chop(1);
  QString name = where.section(QLatin1Char('/'), -1);
  if (m_active->mode() == FilePanel::RootMode)
    name = MainWindow::tr("Computer");
  if (name.isEmpty())
    name = where;
  setWindowTitle(name.isEmpty() ? QStringLiteral("7zipLX") : name + QStringLiteral(" \u2014 7zipLX"));
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
  if (watched == m_filter && event->type() == QEvent::KeyPress)
  {
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Escape)
    {
      m_filter->clear();
      if (m_active) m_active->focusView();
      return true;
    }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Down)
    {
      if (m_active) m_active->focusView();
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
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
  if (!m_twoPanels)
    return;
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
  m_prompts->showError(action + MainWindow::tr(" not supported"), reason);
  return false;
}

bool MainWindow::commitNestedArchive(FilePanel *panel)
{
  if (!panel || !panel->isNestedArchive())
    return true;
  const OperationResult result =
      runOperation(MainWindow::tr("Saving nested archive"),
                   [panel](ProgressState *state, PromptDispatcher *prompts) {
                     return panel->commitNestedArchive(state, prompts);
                   });
  if (!result.ok)
  {
    if (!result.canceled && !result.error.isEmpty())
      m_prompts->showError(MainWindow::tr("Nested archive"), result.error);
    return false;
  }
  QString error;
  if (!panel->refreshNestedParents(nullptr, m_prompts, &error))
  {
    m_prompts->showError(MainWindow::tr("Reload archive"), error);
    return false;
  }
  m_status->setText(MainWindow::tr("Saved nested archive changes"));
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
      this, MainWindow::tr("Open Archive"), QDir::homePath(),
      MainWindow::tr("Archives (*.7z *.zip *.rar *.tar *.gz *.tgz *.bz2 *.xz *.cab *.iso *.wim *.arj *.lzh);;All files (*)"));
  if (!path.isEmpty())
    openPath(path);
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
  m_transfer->performTransfer(transfer, targetPanel, target, action);
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
      m_prompts->showError(MainWindow::tr("Open nested archive"), error);
    return;
  }
  m_status->setText(MainWindow::tr("Opened %1").arg(panel->displayPath()));
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
    m_prompts->showError(MainWindow::tr("Open"), MainWindow::tr("File does not exist:\n%1").arg(path));
    return;
  }
  if (!m_active) return;
  QApplication::setOverrideCursor(Qt::WaitCursor);
  QString error;
  const bool ok = m_active->openArchive(fi.absoluteFilePath(), nullptr, m_prompts, &error);
  QApplication::restoreOverrideCursor();
  if (!ok)
  {
    m_prompts->showError(MainWindow::tr("Cannot open archive"), error);
    return;
  }
  m_status->setText(MainWindow::tr("Opened %1").arg(fi.fileName()));
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
  const OperationResult result = m_runner->run(title, fn);
  if (!result.ok && !result.canceled && !result.error.isEmpty())
    m_status->setText(result.error);
  else if (result.ok)
    m_status->setText(MainWindow::tr("Operation finished"));
  else if (result.canceled)
    m_status->setText(MainWindow::tr("Operation canceled"));
  return result;
}

void MainWindow::extractArchive()
{
  if (!m_active || !m_active->isArchive())
    return;
  ExtractDialog dialog(this);
  dialog.setArchivePath(m_active->displayPath());
  // Next to the archive file on disk: displayPath() may end in a folder
  // inside the archive, and a nested archive lives in a temporary folder.
  const QString archiveDir = QFileInfo(m_active->rootArchivePath()).absolutePath();
  dialog.setDefaultOutputDir(archiveDir);
  if (dialog.exec() != QDialog::Accepted)
    return;

  ExtractOptions options = dialog.options();
  const QVector<int> selected = m_active->selectedArchiveRows();
  ArchiveSession *session = m_active->archiveModel()->session();
  if (options.password.isEmpty())
    options.password = session->password();
  const auto result = runOperation(MainWindow::tr("Extracting"),
                                   [session, selected, options](ProgressState *state, PromptDispatcher *prompts) {
                                     return session->extract(selected, options, state, prompts);
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(MainWindow::tr("Extraction failed"), result.error);
  else if (result.ok)
  {
    m_status->setText(MainWindow::tr("Extracted %1 item(s) to %2").arg(result.files).arg(options.outputDir));
    refreshPanelsShowing(options.outputDir);
  }
}

void MainWindow::refreshPanelsShowing(const QString &dir)
{
  const QString wanted = QFileInfo(dir).absoluteFilePath();
  for (FilePanel *panel : {m_left, m_right})
    if (panel && !panel->isArchive() && panel->mode() == FilePanel::LocalMode &&
        QFileInfo(panel->localPath()).absoluteFilePath() == wanted)
      panel->refresh();
}

void MainWindow::testArchive()
{
  if (!m_active || !m_active->isArchive())
    return;
  const QVector<int> selected = m_active->selectedArchiveRows();
  ArchiveSession *session = m_active->archiveModel()->session();
  ExtractOptions options;
  options.testMode = true;
  const auto result = runOperation(MainWindow::tr("Testing archive"),
                                   [session, selected, options](ProgressState *state, PromptDispatcher *prompts) {
                                     return session->extract(selected, options, state, prompts);
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(MainWindow::tr("Test failed"), result.error);
  else if (result.ok)
    m_prompts->showInfo(MainWindow::tr("Test completed"), MainWindow::tr("No errors were found."));
}

void MainWindow::addToArchive()
{
  if (!m_active || m_active->isArchive())
    return;
  // With nothing selected the current folder is compressed next to itself,
  // never into an archive inside the folder being read.
  const QuickArchiveTarget target =
      quickArchiveTarget(m_active->selectedLocalPaths(), m_active->localPath(), QStringLiteral("7z"));
  const QStringList files = target.inputs;
  CompressDialog dialog(files, QFileInfo(target.archivePath).absolutePath(), this);
  if (dialog.exec() != QDialog::Accepted)
    return;
  const CompressOptions options = dialog.options();
  const auto result = runOperation(MainWindow::tr("Compressing"),
                                   [files, options](ProgressState *state, PromptDispatcher *prompts) {
                                     return compressFiles(files, options, state, prompts);
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(MainWindow::tr("Compression failed"), result.error);
  else if (result.ok)
  {
    m_status->setText(MainWindow::tr("Created %1").arg(options.archivePath));
    showCreatedArchive(options.archivePath);
  }
}

void MainWindow::goUp()
{
  if (m_active && !m_active->goUp())
    m_status->setText(MainWindow::tr("Already at the top"));
  updateActions();
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
  if (!m_active) return;
  m_hash->promptAndHash(m_active);
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

bool MainWindow::revealSecondPanelForTransfer()
{
  if (!m_active || !m_right || m_right == m_active)
    return true;
  if (!m_active->isArchive() && !m_right->isArchive() &&
      QFileInfo(m_active->localPath()).absoluteFilePath() ==
          QFileInfo(m_right->localPath()).absoluteFilePath())
  {
    const QString dir = QFileDialog::getExistingDirectory(
        this, MainWindow::tr("Copy to folder"), QDir::homePath());
    if (dir.isEmpty())
      return false;
    m_right->setLocalPath(dir);
  }
  setTwoPanels(true);
  m_status->setText(MainWindow::tr("Showing the second panel as the copy/move destination"));
  return true;
}

void MainWindow::transferToOtherPanel(bool move)
{
  if (!m_twoPanels && !revealSecondPanelForTransfer())
    return;
  FilePanel *source = m_active;
  FilePanel *target = otherPanel();
  if (!source || !target)
    return;
  const PanelTransfer transfer = source->selectionTransfer();
  if (!transfer.isValid())
  {
    m_status->setText(MainWindow::tr("Nothing selected"));
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
  m_transfer->performTransfer(transfer, target, dropTarget, move ? Qt::MoveAction : Qt::CopyAction, true);
}

void MainWindow::setClipboardFromSelection(bool cut)
{
  if (!m_active)
    return;
  PanelTransfer transfer = m_active->selectionTransfer();
  if (!transfer.isValid())
  {
    m_status->setText(MainWindow::tr("Nothing selected"));
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
  m_status->setText(MainWindow::tr("%1 %2 item(s)")
                        .arg(cut ? MainWindow::tr("Cut") : MainWindow::tr("Copied"))
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
  if (m_transfer->performTransfer(transfer, m_active, target,
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
    m_prompts->showInfo(MainWindow::tr("Split File"), MainWindow::tr("Select one file to split."));
    return;
  }
  const QString src = selected.first();
  int mib = 100;
  if (!InputDialog::getInt(this, MainWindow::tr("Split File"), MainWindow::tr("Split File"),
                           MainWindow::tr("Volume size (MiB):"), 1, 1024 * 64, mib, &mib))
    return;
  const qint64 volumeSize = qint64(mib) * 1024 * 1024;
  const auto result = runOperation(MainWindow::tr("Splitting %1").arg(QFileInfo(src).fileName()),
                                   [src, volumeSize](ProgressState *state, PromptDispatcher *) {
                                     return ::splitFile(src, volumeSize, state);
                                   });
  if (!result.ok && !result.canceled)
  {
    m_prompts->showError(MainWindow::tr("Split File"), result.error);
    return;
  }
  m_active->refresh();
  if (result.ok)
  {
    m_status->setText(MainWindow::tr("Created %n volume(s)", nullptr, int(result.files)));
    m_active->selectLocalPathsWhenLoaded({splitVolumeName(src, 1)});
  }
}

void MainWindow::combineFiles()
{
  if (!m_active || m_active->isArchive()) return;
  const QStringList selected = m_active->selectedLocalPaths();
  if (selected.size() != 1 || !selected.first().endsWith(QStringLiteral(".001")))
  {
    m_prompts->showInfo(MainWindow::tr("Combine Files"), MainWindow::tr("Select the first volume (.001) to combine."));
    return;
  }
  const QString first = selected.first();
  const QString base = first.left(first.size() - 4);
  QString outPath = base;
  if (!InputDialog::getText(this, MainWindow::tr("Combine Files"), MainWindow::tr("Combine Files"),
                            MainWindow::tr("Output file:"), &outPath) ||
      outPath.trimmed().isEmpty())
    return;
  if (QFileInfo(outPath).isRelative())
    outPath = QFileInfo(first).absolutePath() + QLatin1Char('/') + outPath;
  const auto result = runOperation(MainWindow::tr("Combining files"),
                                   [first, outPath](ProgressState *state, PromptDispatcher *) {
                                     return ::combineFiles(first, outPath, state);
                                   });
  if (!result.ok && !result.canceled)
  {
    m_prompts->showError(MainWindow::tr("Combine Files"), result.error);
    return;
  }
  m_active->refresh();
  if (result.ok)
  {
    m_status->setText(MainWindow::tr("Combined %n volume(s) into %1", nullptr, int(result.files)).arg(outPath));
    if (QFileInfo(outPath).absolutePath() == QFileInfo(m_active->localPath()).absoluteFilePath())
      m_active->selectLocalPathsWhenLoaded({outPath});
  }
}

void MainWindow::renameSelected()
{
  if (!m_active) return;
  if (m_active->isArchive())
  {
    if (!ensureWritableArchive(MainWindow::tr("Rename")))
      return;
    const QVector<int> rows = m_active->selectedArchiveRows();
    if (rows.size() != 1)
    {
      m_prompts->showInfo(MainWindow::tr("Rename"), MainWindow::tr("Select one item to rename."));
      return;
    }
    const ArchiveEntryInfo &entry = m_active->archiveModel()->entry(rows.first());
    QString newName = entry.name;
    if (!InputDialog::getText(this, MainWindow::tr("Rename"), MainWindow::tr("Rename"),
                              MainWindow::tr("New name:"), &newName) ||
        newName.isEmpty() || newName == entry.name)
      return;
    if (const QString why = invalidItemName(newName); !why.isEmpty())
    {
      m_prompts->showError(MainWindow::tr("Rename failed"), why);
      return;
    }
    ArchiveSession *session = m_active->archiveModel()->session();
    const auto result = runOperation(MainWindow::tr("Renaming item"),
                                     [session, row = rows.first(), newName](ProgressState *state, PromptDispatcher *prompts) {
                                       return session->renameItem(row, newName, state, prompts);
                                     });
    if (!result.ok && !result.canceled)
      m_prompts->showError(MainWindow::tr("Rename failed"), result.error);
    if (result.ok && m_active->isNestedArchive())
      commitNestedArchive(m_active);
    m_active->archiveModel()->refresh();
    updateActions();
    return;
  }
  const QStringList selected = m_active->selectedLocalPaths();
  if (selected.size() != 1)
  {
    m_prompts->showInfo(MainWindow::tr("Rename"), MainWindow::tr("Select one item to rename."));
    return;
  }
  const QFileInfo fi(selected.first());
  QString newName = fi.fileName();
  if (!InputDialog::getText(this, MainWindow::tr("Rename"), MainWindow::tr("Rename"),
                            MainWindow::tr("New name:"), &newName) ||
      newName.isEmpty() || newName == fi.fileName())
    return;
  if (const QString why = invalidItemName(newName); !why.isEmpty())
  {
    m_prompts->showError(MainWindow::tr("Rename failed"), why);
    return;
  }
  const QString dest = fi.absolutePath() + QLatin1Char('/') + newName;
  if (QFileInfo::exists(dest) || QFileInfo(dest).isSymLink())
  {
    m_prompts->showError(MainWindow::tr("Rename failed"),
                         MainWindow::tr("An item named \"%1\" already exists here.").arg(newName));
    return;
  }
  const bool renamed = fi.isDir() ? QDir().rename(fi.absoluteFilePath(), dest)
                                  : QFile::rename(fi.absoluteFilePath(), dest);
  if (!renamed)
    m_prompts->showError(MainWindow::tr("Rename failed"), MainWindow::tr("Cannot rename:\n%1").arg(fi.absoluteFilePath()));
  m_active->refresh();
  if (renamed)
    m_active->selectLocalPathsWhenLoaded({dest});
}

void MainWindow::deleteSelected()
{
  if (!m_active) return;
  if (m_active->isArchive())
  {
    if (!ensureWritableArchive(MainWindow::tr("Delete")))
      return;
    const QVector<int> rows = m_active->selectedArchiveRows();
    if (rows.isEmpty()) return;
    if (!m_prompts->confirm(MainWindow::tr("Delete from archive"),
                            MainWindow::tr("Delete %1 selected item(s) from the archive?\nThe archive will be rewritten.")
                                .arg(rows.size()), true))
      return;
    ArchiveSession *session = m_active->archiveModel()->session();
    const auto result = runOperation(MainWindow::tr("Deleting items"),
                                     [session, rows](ProgressState *state, PromptDispatcher *prompts) {
                                       return session->deleteItems(rows, state, prompts);
                                     });
    if (!result.ok && !result.canceled)
      m_prompts->showError(MainWindow::tr("Delete failed"), result.error);
    if (result.ok && m_active->isNestedArchive())
      commitNestedArchive(m_active);
    m_active->archiveModel()->refresh();
    updateActions();
    return;
  }
  const QStringList selected = m_active->selectedLocalPaths();
  if (selected.isEmpty()) return;
  if (!m_prompts->confirm(MainWindow::tr("Move to Trash"),
                          MainWindow::tr("Move %n selected item(s) to the trash?", nullptr, int(selected.size()))))
    return;
  // Trash first.  Anything the trash cannot take (another filesystem, no
  // trash support) is only deleted permanently after asking.
  QStringList notTrashed;
  const auto trashed = runOperation(MainWindow::tr("Moving to trash"),
                                    [selected, &notTrashed](ProgressState *state, PromptDispatcher *) {
                                      OperationResult out;
                                      out.ok = true;
                                      for (const QString &path : selected)
                                      {
                                        if (state && state->isCanceled())
                                        {
                                          out.ok = false;
                                          out.canceled = true;
                                          break;
                                        }
                                        if (QFile::moveToTrash(path))
                                          ++out.files;
                                        else
                                          notTrashed << path;
                                      }
                                      return out;
                                    });
  if (!notTrashed.isEmpty() && !trashed.canceled)
  {
    QStringList names;
    for (const QString &path : notTrashed.mid(0, 8))
      names << QFileInfo(path).fileName();
    if (notTrashed.size() > 8)
      names << MainWindow::tr("... and %n more", nullptr, int(notTrashed.size() - 8));
    if (m_prompts->confirm(MainWindow::tr("Delete permanently"),
                           MainWindow::tr("%n item(s) cannot be moved to the trash:", nullptr, int(notTrashed.size())) +
                               QStringLiteral("\n\n") + names.join(QLatin1Char('\n')) + QStringLiteral("\n\n") +
                               MainWindow::tr("Delete them permanently? This cannot be undone."),
                           true))
    {
      const auto removed = runOperation(MainWindow::tr("Deleting"),
                                        [notTrashed](ProgressState *, PromptDispatcher *) {
                                          OperationResult out;
                                          out.ok = true;
                                          QStringList failed;
                                          for (const QString &path : notTrashed)
                                            if (!removeLocalPath(path))
                                              failed << path;
                                          if (!failed.isEmpty())
                                          {
                                            out.ok = false;
                                            out.error = MainWindow::tr("Cannot delete:") + QStringLiteral("\n") +
                                                        failed.join(QLatin1Char('\n'));
                                          }
                                          return out;
                                        });
      if (!removed.ok && !removed.canceled)
        m_prompts->showError(MainWindow::tr("Delete failed"), removed.error);
    }
  }
  else if (trashed.ok)
  {
    m_status->setText(MainWindow::tr("Moved %n item(s) to the trash", nullptr, int(trashed.files)));
  }
  m_active->refresh();
}

void MainWindow::copySelected()
{
  if (!m_active || m_active->isArchive()) return;
  const QStringList selected = m_active->selectedLocalPaths();
  if (selected.isEmpty()) return;
  const QString dest = QFileDialog::getExistingDirectory(this, MainWindow::tr("Copy to folder"), m_active->localPath());
  if (dest.isEmpty()) return;
  const auto result = runOperation(MainWindow::tr("Copying"),
                                   [selected, dest](ProgressState *state, PromptDispatcher *prompts) {
                                     return copyLocalItems(selected, dest, false, state, prompts);
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(MainWindow::tr("Copy failed"), result.error);
  m_active->refresh();
}

void MainWindow::moveSelected()
{
  if (!m_active || m_active->isArchive()) return;
  const QStringList selected = m_active->selectedLocalPaths();
  if (selected.isEmpty()) return;
  const QString dest = QFileDialog::getExistingDirectory(this, MainWindow::tr("Move to folder"), m_active->localPath());
  if (dest.isEmpty()) return;
  const auto result = runOperation(MainWindow::tr("Moving"),
                                   [selected, dest](ProgressState *state, PromptDispatcher *prompts) {
                                     return copyLocalItems(selected, dest, true, state, prompts);
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(MainWindow::tr("Move failed"), result.error);
  m_active->refresh();
}

void MainWindow::createFolder()
{
  if (!m_active) return;
  if (m_active->isArchive())
  {
    if (!ensureWritableArchive(MainWindow::tr("Create folder")))
      return;
    QString name = MainWindow::tr("New Folder");
    if (!InputDialog::getText(this, MainWindow::tr("Create Folder"), MainWindow::tr("Create Folder"),
                              MainWindow::tr("Folder name:"), &name) || name.isEmpty())
      return;
    if (const QString why = invalidItemName(name); !why.isEmpty())
    {
      m_prompts->showError(MainWindow::tr("Create Folder"), why);
      return;
    }
    ArchiveSession *session = m_active->archiveModel()->session();
    const auto result = runOperation(MainWindow::tr("Creating folder"),
                                     [session, name](ProgressState *state, PromptDispatcher *prompts) {
                                       return session->createFolder(name, state, prompts);
                                     });
    if (!result.ok && !result.canceled)
      m_prompts->showError(MainWindow::tr("Create Folder"), result.error);
    if (result.ok && m_active->isNestedArchive())
      commitNestedArchive(m_active);
    m_active->archiveModel()->refresh();
    updateActions();
    return;
  }
  QString name = MainWindow::tr("New Folder");
  if (!InputDialog::getText(this, MainWindow::tr("Create Folder"), MainWindow::tr("Create Folder"),
                            MainWindow::tr("Folder name:"), &name) || name.isEmpty())
    return;
  if (const QString why = invalidItemName(name); !why.isEmpty())
  {
    m_prompts->showError(MainWindow::tr("Create Folder"), why);
    return;
  }
  const QString folder = m_active->localPath() + QLatin1Char('/') + name;
  if (QFileInfo::exists(folder))
  {
    m_prompts->showError(MainWindow::tr("Create Folder"),
                         MainWindow::tr("An item named \"%1\" already exists here.").arg(name));
    return;
  }
  if (!QDir(m_active->localPath()).mkdir(name))
  {
    m_prompts->showError(MainWindow::tr("Create Folder"), MainWindow::tr("Cannot create folder."));
    return;
  }
  m_active->refresh();
  m_active->selectLocalPathsWhenLoaded({folder});
}

void MainWindow::openExternalView()
{
  if (!m_active) return;
  QString error;
  if (!m_externalTools->view(m_active, &error) && !error.isEmpty())
    m_prompts->showError(MainWindow::tr("View"), error);
}

void MainWindow::openExternalEdit()
{
  if (!m_active) return;
  QString error;
  if (!m_externalTools->edit(m_active, &error) && !error.isEmpty())
    m_prompts->showError(MainWindow::tr("Edit"), error);
}

void MainWindow::diffSelected()
{
  if (!m_active) return;
  m_externalTools->diff(m_active);
}

void MainWindow::editComment()
{
  if (!m_active || !m_active->isArchive())
  {
    m_prompts->showInfo(MainWindow::tr("Comment"), MainWindow::tr("Comments are supported for archive items (ZIP)."));
    return;
  }
  if (!ensureWritableArchive(MainWindow::tr("Comment")))
    return;
  const int row = m_active->currentArchiveItem();
  if (row < 0) return;
  const ArchiveEntryInfo &entry = m_active->archiveModel()->entry(row);
  QString comment = entry.comment;
  if (!InputDialog::getMultiLineText(this, MainWindow::tr("Comment"), MainWindow::tr("Comment"),
                                     MainWindow::tr("Comment for %1:").arg(entry.name), &comment))
    return;
  ArchiveSession *session = m_active->archiveModel()->session();
  const auto result = runOperation(MainWindow::tr("Saving comment"),
                                   [session, row, comment](ProgressState *state, PromptDispatcher *prompts) {
                                     return session->setComment(row, comment, state, prompts);
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(MainWindow::tr("Comment failed"), result.error);
  else if (result.ok)
  {
    if (m_active->isNestedArchive())
      commitNestedArchive(m_active);
    m_active->archiveModel()->refresh();
    m_status->setText(MainWindow::tr("Comment saved"));
  }
}

void MainWindow::createLink()
{
  if (!m_active || m_active->isArchive()) return;
  const QStringList selected = m_active->selectedLocalPaths();
  if (selected.size() != 1)
  {
    m_prompts->showInfo(MainWindow::tr("Create Link"), MainWindow::tr("Select one item to link to."));
    return;
  }
  const QFileInfo source(selected.first());
  const QStringList types = {MainWindow::tr("Symbolic link"), MainWindow::tr("Hard link")};
  QString type;
  if (!InputDialog::getItem(this, MainWindow::tr("Create Link"), MainWindow::tr("Create Link"),
                            MainWindow::tr("Link type:"), types, 0, &type))
    return;
  QString linkPath = source.absolutePath() + QLatin1Char('/') + source.fileName() + MainWindow::tr(" link");
  if (!InputDialog::getText(this, MainWindow::tr("Create Link"), MainWindow::tr("Create Link"),
                            MainWindow::tr("Link path:"), &linkPath) ||
      linkPath.isEmpty())
    return;
  if (QFileInfo::exists(linkPath))
  {
    m_prompts->showError(MainWindow::tr("Create Link"), MainWindow::tr("The link path already exists:\n%1").arg(linkPath));
    return;
  }
  bool created = false;
  if (type == types.first())
    created = QFile::link(source.absoluteFilePath(), linkPath);
  else
  {
    const QByteArray src = QFile::encodeName(source.absoluteFilePath());
    const QByteArray dst = QFile::encodeName(linkPath);
    created = (::link(src.constData(), dst.constData()) == 0);
  }
  if (!created)
    m_prompts->showError(MainWindow::tr("Create Link"), MainWindow::tr("Cannot create the link.\nThe target may be on another filesystem."));
  m_active->refresh();
}

void MainWindow::createFile()
{
  if (!m_active || m_active->isArchive()) return;
  QString name = MainWindow::tr("New File.txt");
  if (!InputDialog::getText(this, MainWindow::tr("Create File"), MainWindow::tr("Create File"),
                            MainWindow::tr("File name:"), &name) || name.isEmpty())
    return;
  if (const QString why = invalidItemName(name); !why.isEmpty())
  {
    m_prompts->showError(MainWindow::tr("Create File"), why);
    return;
  }
  const QString path = m_active->localPath() + QLatin1Char('/') + name;
  QFile f(path);
  // NewOnly: never truncate a file that already has this name.
  if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly))
  {
    m_prompts->showError(MainWindow::tr("Create File"),
                         QFileInfo::exists(path)
                             ? MainWindow::tr("An item named \"%1\" already exists here.").arg(name)
                             : MainWindow::tr("Cannot create file."));
    return;
  }
  f.close();
  m_active->refresh();
  m_active->selectLocalPathsWhenLoaded({path});
}

static QString permissionString(QFileDevice::Permissions p, bool dir)
{
  QString out = dir ? QStringLiteral("d") : QStringLiteral("-");
  out += (p & QFileDevice::ReadOwner) ? QLatin1Char('r') : QLatin1Char('-');
  out += (p & QFileDevice::WriteOwner) ? QLatin1Char('w') : QLatin1Char('-');
  out += (p & QFileDevice::ExeOwner) ? QLatin1Char('x') : QLatin1Char('-');
  out += (p & QFileDevice::ReadGroup) ? QLatin1Char('r') : QLatin1Char('-');
  out += (p & QFileDevice::WriteGroup) ? QLatin1Char('w') : QLatin1Char('-');
  out += (p & QFileDevice::ExeGroup) ? QLatin1Char('x') : QLatin1Char('-');
  out += (p & QFileDevice::ReadOther) ? QLatin1Char('r') : QLatin1Char('-');
  out += (p & QFileDevice::WriteOther) ? QLatin1Char('w') : QLatin1Char('-');
  out += (p & QFileDevice::ExeOther) ? QLatin1Char('x') : QLatin1Char('-');
  return out;
}

static QString sizeWithBytes(quint64 bytes)
{
  const QString human = QLocale().formattedDataSize(qint64(bytes), 1, QLocale::DataSizeTraditionalFormat);
  if (bytes < 1024)
    return human;
  return MainWindow::tr("%1 (%2 bytes)").arg(human, QLocale().toString(bytes));
}

void MainWindow::showProperties()
{
  if (!m_active) return;
  if (m_active->isArchive())
  {
    const int row = m_active->currentArchiveItem();
    if (row < 0) return;
    const ArchiveEntryInfo &e = m_active->archiveModel()->entry(row);
    QStringList lines;
    lines << MainWindow::tr("Name: %1").arg(e.name);
    lines << MainWindow::tr("Path in archive: %1").arg(e.path.isEmpty() ? e.name : e.path);
    lines << MainWindow::tr("Type: %1").arg(e.isDir ? MainWindow::tr("Folder") : MainWindow::tr("File"));
    if (!e.isDir)
    {
      lines << MainWindow::tr("Size: %1").arg(sizeWithBytes(e.size));
      if (e.packedSize)
        lines << MainWindow::tr("Packed size: %1").arg(sizeWithBytes(e.packedSize));
    }
    if (e.modified.isValid())
      lines << MainWindow::tr("Modified: %1").arg(QLocale().toString(e.modified, QLocale::LongFormat));
    if (!e.method.isEmpty())
      lines << MainWindow::tr("Method: %1").arg(e.method);
    if (e.hasCrc)
      lines << MainWindow::tr("CRC: %1").arg(e.crc, 8, 16, QLatin1Char('0')).toUpper();
    lines << MainWindow::tr("Encrypted: %1").arg(e.isEncrypted ? MainWindow::tr("Yes") : MainWindow::tr("No"));
    if (!e.comment.isEmpty())
      lines << MainWindow::tr("Comment: %1").arg(e.comment);
    lines << MainWindow::tr("Archive: %1").arg(m_active->rootArchivePath());
    m_prompts->showInfo(MainWindow::tr("Properties"), lines.join(QLatin1Char('\n')));
    return;
  }
  const QStringList selected = m_active->selectedLocalPaths();
  if (selected.size() != 1) return;
  const QFileInfo fi(selected.first());
  QStringList lines;
  lines << MainWindow::tr("Name: %1").arg(fi.fileName());
  lines << MainWindow::tr("Location: %1").arg(fi.absolutePath());
  if (fi.isSymLink())
    lines << MainWindow::tr("Type: Link to %1").arg(fi.symLinkTarget());
  else
    lines << MainWindow::tr("Type: %1").arg(fi.isDir() ? MainWindow::tr("Folder")
                                                       : (isArchiveFileName(fi.fileName())
                                                              ? MainWindow::tr("Archive")
                                                              : MainWindow::tr("File")));
  if (fi.isDir())
  {
    const int count = int(QDir(fi.absoluteFilePath())
                              .entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)
                              .size());
    lines << MainWindow::tr("Contains: %n item(s)", nullptr, count);
  }
  else
  {
    lines << MainWindow::tr("Size: %1").arg(sizeWithBytes(quint64(fi.size())));
  }
  lines << MainWindow::tr("Modified: %1").arg(QLocale().toString(fi.lastModified(), QLocale::LongFormat));
  lines << MainWindow::tr("Permissions: %1").arg(permissionString(fi.permissions(), fi.isDir()));
  lines << MainWindow::tr("Owner: %1:%2").arg(fi.owner(), fi.group());
  m_prompts->showInfo(MainWindow::tr("Properties"), lines.join(QLatin1Char('\n')));
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
  m_status->setText(MainWindow::tr("Added to favorites: %1").arg(path));
}

void MainWindow::clearFavorites()
{
  QSettings s;
  s.setValue(QStringLiteral("favorites"), QStringList());
  m_status->setText(MainWindow::tr("Favorites cleared"));
}

void MainWindow::showOptions()
{
  QSettings settings;
  const QString viewer = settings.value(QStringLiteral("tools/viewerCommand"), QStringLiteral("xdg-open")).toString();
  const QString editor = settings.value(QStringLiteral("tools/editorCommand"), QStringLiteral("xdg-open")).toString();
  const QString diff = settings.value(QStringLiteral("tools/diffCommand"), QStringLiteral("diff -u")).toString();
  const QString language = settings.value(QStringLiteral("ui/language"), QStringLiteral("system")).toString();
  const QString theme = settings.value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString();
  OptionsDialog dialog(m_showHiddenAction && m_showHiddenAction->isChecked(),
                       m_showDotsAction && m_showDotsAction->isChecked(),
                       m_left && m_left->view()->showGrid(),
                       m_flatViewAction && m_flatViewAction->isChecked(),
                       viewer, editor, diff, language, theme, this);
  const bool menus = settings.value(QStringLiteral("integration/fileManagerMenus"), true).toBool();
  dialog.setFileManagerMenus(menus);
  if (dialog.exec() != QDialog::Accepted) return;
  if (dialog.fileManagerMenus() != menus)
  {
    settings.setValue(QStringLiteral("integration/fileManagerMenus"), dialog.fileManagerMenus());
    QString error;
    if (!DesktopIntegration::syncFileManagerScripts(dialog.fileManagerMenus(), &error))
      m_prompts->showError(MainWindow::tr("File manager menus"), error);
  }
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
  settings.setValue(QStringLiteral("ui/theme"), dialog.theme());
  Theme::apply(*qApp, Theme::modeFromString(dialog.theme()));
  updateThemeIcons();
}

void MainWindow::showBenchmark()
{
  BenchmarkDialog dialog(this);
  connect(&dialog, &BenchmarkDialog::runRequested, this, [this, &dialog] {
    const BenchmarkOptions options = dialog.options();
    const auto result = runOperation(MainWindow::tr("Benchmark"),
                                     [options](ProgressState *state, PromptDispatcher *prompts) {
                                       return runBenchmark(options, state, prompts).operation;
                                     });
    dialog.setLog(result.details);
    dialog.setRunning(false);
    if (!result.ok && !result.canceled)
      m_prompts->showError(MainWindow::tr("Benchmark failed"), result.error);
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
  for (const QString &path : candidates)
  {
    const QFileInfo fi(path);
    if (fi.isFile() && isArchiveFileName(fi.fileName()))
      result << fi.absoluteFilePath();
  }
  return result;
}

void MainWindow::openCurrentItem()
{
  if (!m_active) return;
  if (m_active->isArchive())
  {
    if (m_active->isParentRow(m_active->currentRow()))
    {
      goUp();
      updateActions();
      return;
    }
    const int row = m_active->currentArchiveItem();
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
    if (isArchiveFileName(entry.name))
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
    m_prompts->showError(MainWindow::tr("Open Inside"), error);
    return;
  }
  m_status->setText(MainWindow::tr("Opened %1").arg(fi.fileName()));
  updateActions();
}

void MainWindow::openCurrentArchiveInside(const QString &formatHint)
{
  if (!m_active || !m_active->isArchive()) return;
  const int row = m_active->currentArchiveItem();
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
      m_prompts->showError(MainWindow::tr("Open Inside"), error);
    return;
  }
  m_status->setText(MainWindow::tr("Opened %1").arg(m_active->displayPath()));
  updateActions();
}

void MainWindow::openOutside()
{
  if (!m_active) return;
  QString error;
  if (!m_externalTools->openWithDesktop(m_active, &error) && !error.isEmpty())
    m_prompts->showError(MainWindow::tr("Open Outside"), error);
}

void MainWindow::extractHere(bool toFolder)
{
  if (!m_active) return;
  if (!m_active->isArchive())
  {
    extractLocalArchives(toFolder ? LocalExtract::ToFolders : LocalExtract::Here);
    return;
  }
  ArchiveSession *session = m_active->archiveModel()->session();
  if (!session) return;
  const QVector<int> rows = m_active->selectedArchiveRows();
  ExtractOptions options;
  options.outputDir = QFileInfo(m_active->rootArchivePath()).absolutePath();
  if (toFolder)
    options.outputDir += QLatin1Char('/') + archiveFolderName(m_active->rootArchivePath());
  options.pathMode = 0;
  options.overwriteMode = 0;
  options.password = session->password();
  const auto result = runOperation(MainWindow::tr("Extracting"),
                                   [session, rows, options](ProgressState *state, PromptDispatcher *prompts) {
                                     return session->extract(rows, options, state, prompts);
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(MainWindow::tr("Extraction failed"), result.error);
  else if (result.ok)
  {
    m_status->setText(MainWindow::tr("Extracted %1 item(s) to %2").arg(result.files).arg(options.outputDir));
    refreshPanelsShowing(QFileInfo(m_active->rootArchivePath()).absolutePath());
  }
}

void MainWindow::extractLocalArchives(LocalExtract mode)
{
  if (!m_active || m_active->isArchive()) return;
  const QStringList archives = selectedLocalArchiveFiles();
  if (archives.isEmpty())
  {
    m_prompts->showInfo(MainWindow::tr("Extract"), MainWindow::tr("Select one or more archive files first."));
    return;
  }
  ExtractOptions base;
  base.outputDir = QFileInfo(archives.first()).absolutePath();
  base.pathMode = 0;
  base.overwriteMode = 0;
  if (mode == LocalExtract::Dialog)
  {
    ExtractDialog dialog(this);
    dialog.setArchivePath(archives.size() == 1
        ? archives.first()
        : MainWindow::tr("%n archive(s)", nullptr, int(archives.size())));
    dialog.setDefaultOutputDir(archives.size() == 1
        ? base.outputDir + QLatin1Char('/') + archiveFolderName(archives.first())
        : base.outputDir);
    if (dialog.exec() != QDialog::Accepted)
      return;
    base = dialog.options();
  }
  if (base.outputDir.isEmpty()) return;
  const bool toFolders = mode == LocalExtract::ToFolders;
  const auto result = runOperation(MainWindow::tr("Extracting archives"),
                                   [archives, base, toFolders](ProgressState *state, PromptDispatcher *prompts) {
                                     OperationResult total;
                                     total.ok = true;
                                     QStringList errors;
                                     for (const QString &path : archives)
                                     {
                                       if (state && state->isCanceled())
                                       {
                                         total.ok = false;
                                         total.canceled = true;
                                         break;
                                       }
                                       ArchiveSession session;
                                       QString openError;
                                       if (!session.open(path, QString(), state, prompts, &openError, base.password))
                                       {
                                         total.ok = false;
                                         errors << QFileInfo(path).fileName() + QStringLiteral(": ") + openError;
                                         continue;
                                       }
                                       ExtractOptions eo = base;
                                       if (toFolders)
                                         eo.outputDir = QFileInfo(path).absolutePath() + QLatin1Char('/') +
                                                        archiveFolderName(path);
                                       if (eo.password.isEmpty())
                                         eo.password = session.password();
                                       auto result = session.extract({}, eo, state, prompts);
                                       total.files += result.files;
                                       total.folders += result.folders;
                                       total.unpackedSize += result.unpackedSize;
                                       total.messages += result.messages;
                                       session.close();
                                       if (result.canceled)
                                       {
                                         total.ok = false;
                                         total.canceled = true;
                                         break;
                                       }
                                       if (!result.ok)
                                       {
                                         total.ok = false;
                                         errors << QFileInfo(path).fileName() + QStringLiteral(": ") + result.error;
                                       }
                                     }
                                     total.error = errors.join(QLatin1Char('\n'));
                                     return total;
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(MainWindow::tr("Extraction failed"), result.error);
  else if (result.ok)
    m_status->setText(MainWindow::tr("Extracted %1 item(s) from %2 archive(s)")
                          .arg(result.files).arg(archives.size()));
  m_active->refresh();
  refreshPanelsShowing(base.outputDir);
}

void MainWindow::testLocalArchives()
{
  if (!m_active || m_active->isArchive()) return;
  const QStringList archives = selectedLocalArchiveFiles();
  if (archives.isEmpty())
  {
    m_prompts->showInfo(MainWindow::tr("Test archive"), MainWindow::tr("Select one or more archive files first."));
    return;
  }
  const auto result = runOperation(MainWindow::tr("Testing archives"),
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
                                         total.error += (total.error.isEmpty() ? QString() : QStringLiteral("\n")) +
                                                        QFileInfo(path).fileName() + QStringLiteral(": ") + openError;
                                         continue;
                                       }
                                       ExtractOptions eo;
                                       eo.testMode = true;
                                       eo.password = session.password();
                                       auto result = session.extract({}, eo, state, prompts);
                                       total.files += result.files;
                                       total.unpackedSize += result.unpackedSize;
                                       total.messages += result.messages;
                                       session.close();
                                       if (result.canceled)
                                       {
                                         total.ok = false;
                                         total.canceled = true;
                                         return total;
                                       }
                                       if (!result.ok)
                                       {
                                         total.ok = false;
                                         total.error += (total.error.isEmpty() ? QString() : QStringLiteral("\n")) +
                                                        QFileInfo(path).fileName() + QStringLiteral(": ") + result.error;
                                       }
                                     }
                                     return total;
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(MainWindow::tr("Test failed"), result.error);
  else if (result.ok)
    m_prompts->showInfo(MainWindow::tr("Test completed"),
                        MainWindow::tr("No errors were found in %n archive(s).", nullptr, int(archives.size())) +
                            QLatin1Char('\n') +
                            MainWindow::tr("%1 file(s) tested.").arg(result.files));
}

static QString menuSafe(QString text)
{
  // A single '&' in menu text would turn into a mnemonic underline.
  return text.replace(QLatin1Char('&'), QStringLiteral("&&"));
}

void MainWindow::compressSelectionQuick(const QString &extension)
{
  if (!m_active || m_active->isArchive()) return;
  const QuickArchiveTarget target =
      quickArchiveTarget(m_active->selectedLocalPaths(), m_active->localPath(), extension);
  const QString archivePath = target.archivePath;
  const QString archiveName = QFileInfo(archivePath).fileName();
  for (const QString &input : target.inputs)
  {
    if (QFileInfo(input).absoluteFilePath() == QFileInfo(archivePath).absoluteFilePath())
    {
      m_prompts->showError(MainWindow::tr("Compression failed"),
                           MainWindow::tr("The output archive cannot be one of the input files."));
      return;
    }
  }
  // Like 7-Zip, the quick action adds to an archive that already exists
  // (items with the same name are updated); say so instead of implying the
  // old archive is thrown away.
  if (QFileInfo::exists(archivePath) &&
      !m_prompts->confirm(MainWindow::tr("Update archive"),
                          MainWindow::tr("%1 already exists.\nAdd the selected item(s) to it?")
                              .arg(archiveName)))
    return;
  CompressOptions options;
  options.archivePath = archivePath;
  options.formatIndex = SevenZipEngine::instance().formatIndex(extension == QLatin1String("zip")
      ? QStringLiteral("zip") : QStringLiteral("7z"));
  options.level = 5;
  const QStringList files = target.inputs;
  const auto result = runOperation(MainWindow::tr("Compressing to %1").arg(archiveName),
                                   [files, options](ProgressState *state, PromptDispatcher *prompts) {
                                     return compressFiles(files, options, state, prompts);
                                   });
  if (!result.ok && !result.canceled)
    m_prompts->showError(MainWindow::tr("Compression failed"), result.error);
  else if (result.ok)
  {
    m_status->setText(MainWindow::tr("Created %1 (%n item(s))", nullptr, int(files.size())).arg(archivePath));
    showCreatedArchive(archivePath);
  }
}

void MainWindow::showCreatedArchive(const QString &archivePath)
{
  if (!m_active || m_active->isArchive())
    return;
  m_active->refresh();
  if (QFileInfo(archivePath).absolutePath() == QFileInfo(m_active->localPath()).absoluteFilePath())
    m_active->selectLocalPathsWhenLoaded({archivePath});
  FilePanel *other = otherPanel();
  if (m_twoPanels && other && !other->isArchive() &&
      QFileInfo(other->localPath()).absoluteFilePath() == QFileInfo(archivePath).absolutePath())
    other->refresh();
}

void MainWindow::hashSelection(const QString &method)
{
  if (!m_active) return;
  m_hash->hashSelection(m_active, method);
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
    seven->addAction(MainWindow::tr("Open archive"), this, [this] { openCurrentItem(); })
        ->setEnabled(archiveFiles.size() == 1);
    seven->addAction(MainWindow::tr("Extract files..."), this, [this] { extractLocalArchives(LocalExtract::Dialog); })
        ->setEnabled(hasArchive);
    seven->addAction(MainWindow::tr("Extract Here"), this, [this] { extractLocalArchives(LocalExtract::Here); })
        ->setEnabled(hasArchive);
    const QString folderLabel = archiveFiles.size() == 1
        ? MainWindow::tr("Extract to \"%1/\"").arg(menuSafe(fontMetrics().elidedText(
              archiveFolderName(archiveFiles.first()), Qt::ElideMiddle, 260)))
        : MainWindow::tr("Extract each to its own folder");
    seven->addAction(folderLabel, this, [this] { extractLocalArchives(LocalExtract::ToFolders); })
        ->setEnabled(hasArchive);
    seven->addAction(MainWindow::tr("Test archive"), this, [this] { testLocalArchives(); })->setEnabled(hasArchive);
    seven->addSeparator();
    seven->addAction(MainWindow::tr("Add to archive..."), this, &MainWindow::addToArchive);
    const QStringList quickInputs = panel->selectedLocalPaths();
    const QString quickBase = quickArchiveTarget(quickInputs, panel->localPath(), QStringLiteral("7z")).baseName;
    const QString shownBase = menuSafe(fontMetrics().elidedText(quickBase, Qt::ElideMiddle, 260));
    QAction *quick7z = seven->addAction(MainWindow::tr("Add to \"%1.7z\"").arg(shownBase), this,
                                        [this] { compressSelectionQuick(QStringLiteral("7z")); });
    QAction *quickZip = seven->addAction(MainWindow::tr("Add to \"%1.zip\"").arg(shownBase), this,
                                         [this] { compressSelectionQuick(QStringLiteral("zip")); });
    const QString quickTip = quickInputs.isEmpty()
        ? MainWindow::tr("Compress the current folder")
        : MainWindow::tr("Compress %n selected item(s)", nullptr, int(quickInputs.size()));
    quick7z->setToolTip(quickTip);
    quickZip->setToolTip(quickTip);
    seven->setToolTipsVisible(true);
    menu.addSeparator();

    add(MainWindow::tr("Open"), [this] { openCurrentItem(); }, selectedCount == 1, QKeySequence(Qt::Key_Return));
    add(MainWindow::tr("Open Inside"), [this] { openInside(QString()); }, selectedCount == 1, QKeySequence(QStringLiteral("Ctrl+PgDown")));
    add(MainWindow::tr("Open Outside"), [this] { openOutside(); }, selectedCount == 1, QKeySequence(QStringLiteral("Shift+Return")));
    add(MainWindow::tr("View"), [this] { openExternalView(); }, selectedCount == 1, QKeySequence(Qt::Key_F3));
    add(MainWindow::tr("Edit"), [this] { openExternalEdit(); }, selectedCount == 1, QKeySequence(Qt::Key_F4));
    menu.addSeparator();
    add(MainWindow::tr("Rename..."), [this] { renameSelected(); }, selectedCount == 1, QKeySequence(Qt::Key_F2));
    add(MainWindow::tr("Copy to Other Panel"), [this] { copyToOtherPanel(); }, selectedCount >= 1, QKeySequence(Qt::Key_F5));
    add(MainWindow::tr("Move to Other Panel"), [this] { moveToOtherPanel(); }, selectedCount >= 1, QKeySequence(Qt::Key_F6));
    add(MainWindow::tr("Copy To..."), [this] { copySelected(); }, selectedCount >= 1, QKeySequence(QStringLiteral("Ctrl+F5")));
    add(MainWindow::tr("Move To..."), [this] { moveSelected(); }, selectedCount >= 1, QKeySequence(QStringLiteral("Ctrl+F6")));
    add(MainWindow::tr("Move to Trash"), [this] { deleteSelected(); }, selectedCount >= 1, QKeySequence(Qt::Key_Delete));
    menu.addSeparator();
    add(MainWindow::tr("Properties"), [this] { showProperties(); }, selectedCount == 1, QKeySequence(QStringLiteral("Alt+Return")));
    QMenu *crc = menu.addMenu(MainWindow::tr("CRC"));
    for (const QString &method : {QStringLiteral("CRC32"), QStringLiteral("CRC64"), QStringLiteral("MD5"),
                                  QStringLiteral("SHA1"), QStringLiteral("SHA256"), QStringLiteral("SHA512"),
                                  QStringLiteral("SHA3-256"), QStringLiteral("BLAKE2sp"), QStringLiteral("XXH64")})
      crc->addAction(method, this, [this, method] { hashSelection(method); });
    QMenu *more = menu.addMenu(MainWindow::tr("More"));
    auto addMore = [more, this](const QString &text, const std::function<void()> &fn, bool enabled) {
      more->addAction(text, this, fn)->setEnabled(enabled);
    };
    addMore(MainWindow::tr("Open Inside *"), [this] { openInside(QStringLiteral("*")); }, selectedCount == 1);
    addMore(MainWindow::tr("Open Inside #"), [this] { openInside(QStringLiteral("#")); }, selectedCount == 1);
    more->addSeparator();
    addMore(MainWindow::tr("Split file..."), [this] { splitFile(); }, selectedCount == 1);
    addMore(MainWindow::tr("Combine files..."), [this] { combineFiles(); }, selectedCount == 1);
    addMore(MainWindow::tr("Link..."), [this] { createLink(); }, selectedCount == 1);
    menu.addSeparator();
    add(MainWindow::tr("Create Folder..."), [this] { createFolder(); }, true, QKeySequence(Qt::Key_F7));
    add(MainWindow::tr("Create File..."), [this] { createFile(); }, true, QKeySequence(QStringLiteral("Ctrl+N")));
    menu.addSeparator();
    add(MainWindow::tr("Select All"), [panel] { panel->selectAllRows(); }, true, QKeySequence(QKeySequence::SelectAll));
    add(MainWindow::tr("Invert Selection"), [panel] { panel->invertSelection(); });
    add(MainWindow::tr("Refresh"), [panel] { panel->refresh(); }, true, QKeySequence(QStringLiteral("Ctrl+R")));
  }
  else
  {
    add(MainWindow::tr("Open"), [this] { openCurrentItem(); }, selectedCount == 1, QKeySequence(Qt::Key_Return));
    add(MainWindow::tr("Open Inside"), [this] { openCurrentItem(); }, selectedCount == 1, QKeySequence(QStringLiteral("Ctrl+PgDown")));
    add(MainWindow::tr("Open Outside"), [this] { openOutside(); }, selectedCount == 1, QKeySequence(QStringLiteral("Shift+Return")));
    add(MainWindow::tr("View"), [this] { openExternalView(); }, selectedCount == 1, QKeySequence(Qt::Key_F3));
    add(MainWindow::tr("Edit"), [this] { openExternalEdit(); }, selectedCount == 1, QKeySequence(Qt::Key_F4));
    menu.addSeparator();
    add(MainWindow::tr("Extract files..."), [this] { extractArchive(); }, true, QKeySequence(QStringLiteral("Ctrl+E")));
    add(MainWindow::tr("Extract Here"), [this] { extractHere(false); }, true);
    add(MainWindow::tr("Extract to \"%1/\"").arg(menuSafe(fontMetrics().elidedText(
            archiveFolderName(panel->rootArchivePath()), Qt::ElideMiddle, 260))),
        [this] { extractHere(true); }, true);
    add(MainWindow::tr("Test archive"), [this] { testArchive(); }, true, QKeySequence(QStringLiteral("Ctrl+T")));
    menu.addSeparator();
    add(MainWindow::tr("Rename..."), [this] { renameSelected(); }, selectedCount == 1, QKeySequence(Qt::Key_F2));
    add(MainWindow::tr("Copy to Other Panel"), [this] { copyToOtherPanel(); }, selectedCount >= 1, QKeySequence(Qt::Key_F5));
    add(MainWindow::tr("Move to Other Panel"), [this] { moveToOtherPanel(); }, selectedCount >= 1, QKeySequence(Qt::Key_F6));
    add(MainWindow::tr("Delete"), [this] { deleteSelected(); }, selectedCount >= 1, QKeySequence(Qt::Key_Delete));
    add(MainWindow::tr("Comment..."), [this] { editComment(); }, selectedCount == 1, QKeySequence(QStringLiteral("Ctrl+Z")));
    add(MainWindow::tr("Properties"), [this] { showProperties(); }, selectedCount == 1, QKeySequence(QStringLiteral("Alt+Return")));
    QMenu *crc = menu.addMenu(MainWindow::tr("CRC"));
    for (const QString &method : {QStringLiteral("CRC32"), QStringLiteral("CRC64"), QStringLiteral("MD5"),
                                  QStringLiteral("SHA1"), QStringLiteral("SHA256"), QStringLiteral("SHA512"),
                                  QStringLiteral("SHA3-256"), QStringLiteral("BLAKE2sp"), QStringLiteral("XXH64")})
      crc->addAction(method, this, [this, method] { hashSelection(method); });
    QMenu *more = menu.addMenu(MainWindow::tr("More"));
    more->addAction(MainWindow::tr("Open Inside *"), this, [this] { openCurrentArchiveInside(QStringLiteral("*")); })
        ->setEnabled(selectedCount == 1);
    more->addAction(MainWindow::tr("Open Inside #"), this, [this] { openCurrentArchiveInside(QStringLiteral("#")); })
        ->setEnabled(selectedCount == 1);
    menu.addSeparator();
    add(MainWindow::tr("Create Folder..."), [this] { createFolder(); }, true, QKeySequence(Qt::Key_F7));
    menu.addSeparator();
    add(MainWindow::tr("Select All"), [panel] { panel->selectAllRows(); }, true, QKeySequence(QKeySequence::SelectAll));
    add(MainWindow::tr("Invert Selection"), [panel] { panel->invertSelection(); });
    add(MainWindow::tr("Refresh"), [panel] { panel->refresh(); }, true, QKeySequence(QStringLiteral("Ctrl+R")));
  }

  menu.setObjectName(QStringLiteral("contextMenu"));
  menu.exec(globalPos);
}

void MainWindow::showAbout()
{
  AboutDialog dialog(this);
  dialog.exec();
}
