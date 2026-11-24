// End-to-end tests of the main window: every context-menu and toolbar action
// is driven the way a user would (right-click, pick the menu entry, answer
// the dialog), on the offscreen platform.

#include <QtTest/QtTest>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableView>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <deque>
#include <functional>

#include "app/CommandLine.h"
#include "app/DesktopIntegration.h"
#include "app/MainWindow.h"
#include "backend/ArchiveSession.h"
#include "backend/CompressOperation.h"
#include "backend/PromptDispatcher.h"
#include "backend/SevenZipEngine.h"
#include "models/ArchiveFolderModel.h"
#include "models/LocalFsModel.h"
#include "ui/Theme.h"
#include "ui/dialogs/CompressDialog.h"
#include "ui/dialogs/ExtractDialog.h"
#include "ui/dialogs/HashResultDialog.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/widgets/FilePanel.h"

namespace {

QString plain(QString text)
{
  text.remove(QLatin1Char('&'));
  return text;
}

bool writeFile(const QString &path, const QByteArray &contents)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(contents) == contents.size();
}

QByteArray readFile(const QString &path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString dialogText(QWidget *dialog)
{
  QStringList parts;
  for (QLabel *label : dialog->findChildren<QLabel *>())
    if (label->isVisible() && !label->text().isEmpty())
      parts << label->text();
  for (QPlainTextEdit *edit : dialog->findChildren<QPlainTextEdit *>())
    parts << edit->toPlainText();
  for (QTextEdit *edit : dialog->findChildren<QTextEdit *>())
    parts << edit->toPlainText();
  return parts.join(QLatin1Char('\n'));
}

QPushButton *buttonNamed(QWidget *dialog, const QString &text)
{
  for (QPushButton *button : dialog->findChildren<QPushButton *>())
    if (button->isVisible() && plain(button->text()) == text)
      return button;
  return nullptr;
}

void clickAccept(QWidget *dialog)
{
  for (QDialogButtonBox *box : dialog->findChildren<QDialogButtonBox *>())
  {
    for (QAbstractButton *button : box->buttons())
    {
      const auto role = box->buttonRole(button);
      if (role == QDialogButtonBox::AcceptRole || role == QDialogButtonBox::YesRole)
      {
        button->click();
        return;
      }
    }
  }
  if (auto *d = qobject_cast<QDialog *>(dialog))
    d->accept();
}

// Answers modal dialogs in order and records anything unexpected.
class DialogScript : public QObject
{
public:
  using Handler = std::function<void(QWidget *)>;

  DialogScript()
  {
    m_timer.setInterval(15);
    connect(&m_timer, &QTimer::timeout, this, [this] { poll(); });
    m_timer.start();
  }

  void expect(const QString &what, Handler handler) { m_queue.push_back({what, std::move(handler)}); }
  bool done() const { return m_queue.empty(); }
  QStringList unexpected;
  QStringList seen; // text of every dialog handled, in order

  void reset()
  {
    m_queue.clear();
    unexpected.clear();
    seen.clear();
  }

private:
  void poll()
  {
    QWidget *modal = QApplication::activeModalWidget();
    if (!modal || modal == m_last || !modal->isVisible())
      return;
    // Progress windows appear on their own for slower work (debug builds);
    // they are not prompts to answer.
    if (qobject_cast<ProgressDialog *>(modal))
      return;
    m_last = modal;
    const QString text = modal->windowTitle() + QStringLiteral(" | ") + dialogText(modal);
    seen << text;
    if (m_queue.empty())
    {
      unexpected << text;
      if (auto *d = qobject_cast<QDialog *>(modal))
        QTimer::singleShot(0, d, &QDialog::reject);
      return;
    }
    auto step = std::move(m_queue.front());
    m_queue.pop_front();
    QPointer<QWidget> guard(modal);
    QTimer::singleShot(0, modal, [step, guard] {
      if (guard)
        step.second(guard);
    });
    // A dialog can be reused by a later prompt only after it closes.
    connect(modal, &QObject::destroyed, this, [this, modal] {
      if (m_last == modal)
        m_last = nullptr;
    });
    QTimer::singleShot(0, this, [this, guard] {
      if (!guard || !guard->isVisible())
        m_last = nullptr;
    });
  }

  QTimer m_timer;
  QPointer<QWidget> m_last;
  std::deque<std::pair<QString, Handler>> m_queue;
};

} // namespace

class MainWindowActionsTest : public QObject
{
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // Local folder context menu.
  void quickCompressKeepsWholeSelection();
  void quickZipSingleFile();
  void addToArchiveDialog();
  void extractHereFromMenu();
  void extractToFolderFromMenu();
  void extractFilesDialogFromMenu();
  void extractEachToOwnFolder();
  void testArchiveFromMenu();
  void openArchiveFromMenuAndGoUp();
  void debPackagesAreArchives();
  void renameLocalItem();
  void renameRejectsSlash();
  void createFolderAndFile();
  void createFileNeverTruncates();
  void deleteMovesToTrash();
  void splitAndCombine();
  void propertiesAreReadable();
  void hashSelection();
  void copyAndMoveToOtherPanel();
  void createSymbolicLink();
  void selectAllAndInvert();

  // Archive context menu.
  void archiveExtractHereFromSubfolder();
  void archiveExtractToFolder();
  void archiveExtractDialogDefaultsNextToArchive();
  void archiveTestFromMenu();
  void archiveRenameDeleteCreateFolder();
  void archiveCommentZip();
  void archiveProperties();
  void archiveCopyToOtherPanel();
  void nestedArchiveExtractHere();

  void archiveViewMaterializesItem();
  void archiveEditWritesBack();
  void archiveFlatView();

  // Toolbar, menu bar and keyboard.
  void toolbarActionsFollowMode();
  void clipboardCopyPaste();
  void toolsHashDialog();
  void optionsDialogApplies();
  void favoritesAndComputer();
  void benchmarkDialogOpens();
  void filterBoxFiltersActivePanel();

  // File manager integration (command line jobs and desktop scripts).
  void cliCompressQuick();
  void cliExtractModes();
  void cliTestReportsResult();
  void cliAddDialog();
  void desktopScriptsInstallAndRun();

private:
  QString work(const QString &name = QString());
  MainWindow *window() const { return m_window.get(); }
  FilePanel *left() const;
  FilePanel *right() const;
  void openFolder(FilePanel *panel, const QString &dir);
  void waitLocal(FilePanel *panel);
  int rowOf(FilePanel *panel, const QString &name) const;
  void select(FilePanel *panel, const QStringList &names);
  // Right-clicks |rowName| (or empty space) and triggers the entry at |path|.
  // Returns an empty string on success or why the entry could not be used.
  QString menu(FilePanel *panel, const QString &rowName, const QStringList &path);
  QStringList menuEntries(FilePanel *panel, const QString &rowName, const QString &submenu = QString());
  QString makeArchive(const QString &path, const QStringList &inputs);
  QStringList archiveNames(const QString &archive);
  void finishScript();

  QTemporaryDir m_root;
  std::unique_ptr<MainWindow> m_window;
  DialogScript *m_script = nullptr;
  QString m_case;
  int m_counter = 0;
};

void MainWindowActionsTest::initTestCase()
{
  QVERIFY(m_root.isValid());
  QStandardPaths::setTestModeEnabled(true);
  QSettings().clear();
  QString error;
  QVERIFY2(SevenZipEngine::instance().initialize(&error), qPrintable(error));
  Theme::apply(*qApp, Theme::Mode::Dark);
  m_script = new DialogScript;
}

void MainWindowActionsTest::cleanupTestCase()
{
  delete m_script;
  QSettings().clear();
  SevenZipEngine::instance().shutdown();
}

void MainWindowActionsTest::init()
{
  m_case = m_root.path() + QStringLiteral("/case%1").arg(++m_counter);
  QDir().mkpath(m_case);
  QSettings settings;
  settings.clear();
  settings.setValue(QStringLiteral("window/leftPath"), m_case);
  settings.setValue(QStringLiteral("window/rightPath"), m_case);
  settings.setValue(QStringLiteral("tools/viewerCommand"), QStringLiteral("true"));
  settings.setValue(QStringLiteral("tools/editorCommand"), QStringLiteral("true"));
  settings.sync();
  m_script->reset();
  m_window = std::make_unique<MainWindow>();
  m_window->resize(1200, 800);
  m_window->show();
  QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
  waitLocal(left());
}

void MainWindowActionsTest::cleanup()
{
  if (!m_script->unexpected.isEmpty())
    qWarning("Unexpected dialogs: %s", qPrintable(m_script->unexpected.join(QStringLiteral(" || "))));
  m_window.reset();
  QTest::qWait(10);
}

QString MainWindowActionsTest::work(const QString &name)
{
  return name.isEmpty() ? m_case : m_case + QLatin1Char('/') + name;
}

FilePanel *MainWindowActionsTest::left() const
{
  for (FilePanel *panel : m_window->findChildren<FilePanel *>())
    if (panel->panelId() == 0)
      return panel;
  return nullptr;
}

FilePanel *MainWindowActionsTest::right() const
{
  for (FilePanel *panel : m_window->findChildren<FilePanel *>())
    if (panel->panelId() == 1)
      return panel;
  return nullptr;
}

void MainWindowActionsTest::waitLocal(FilePanel *panel)
{
  QTRY_VERIFY_WITH_TIMEOUT(panel->mode() != FilePanel::LocalMode || !panel->localModel()->isLoading(), 5000);
  QTest::qWait(30);
}

void MainWindowActionsTest::openFolder(FilePanel *panel, const QString &dir)
{
  panel->setLocalPath(dir);
  waitLocal(panel);
}

int MainWindowActionsTest::rowOf(FilePanel *panel, const QString &name) const
{
  QAbstractItemModel *model = panel->view()->model();
  for (int row = 0; row < model->rowCount(); ++row)
  {
    QString text = model->data(model->index(row, 0)).toString();
    while (text.endsWith(QLatin1Char('/')))
      text.chop(1);
    if (text == name)
      return row;
  }
  return -1;
}

void MainWindowActionsTest::select(FilePanel *panel, const QStringList &names)
{
  QItemSelectionModel *selection = panel->view()->selectionModel();
  selection->clearSelection();
  QAbstractItemModel *model = panel->view()->model();
  for (const QString &name : names)
  {
    const int row = rowOf(panel, name);
    QVERIFY2(row >= 0, qPrintable(name));
    selection->select(model->index(row, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection->setCurrentIndex(model->index(row, 0), QItemSelectionModel::NoUpdate);
  }
}

static QAction *findAction(QMenu *menu, const QString &label)
{
  for (QAction *action : menu->actions())
    if (!action->isSeparator() && plain(action->text()).startsWith(label))
      return action;
  return nullptr;
}

QString MainWindowActionsTest::menu(FilePanel *panel, const QString &rowName, const QStringList &path)
{
  QTableView *view = panel->view();
  QPoint pos(20, view->viewport()->height() - 5);
  if (!rowName.isEmpty())
  {
    const int row = rowOf(panel, rowName);
    if (row < 0)
      return QStringLiteral("no row %1").arg(rowName);
    view->scrollTo(view->model()->index(row, 0));
    pos = view->visualRect(view->model()->index(row, 1)).center();
    // The press goes through FileView, like a real right-click.
    QTest::mousePress(view->viewport(), Qt::RightButton, {}, pos);
    QTest::mouseRelease(view->viewport(), Qt::RightButton, {}, pos);
  }

  QString failure = QStringLiteral("menu did not open");
  int level = 0;
  QPointer<QMenu> current;
  QTimer driver;
  driver.setInterval(10);
  connect(&driver, &QTimer::timeout, this, [&] {
    auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    if (!popup || popup == current)
      return;
    current = popup;
    QAction *action = findAction(popup, path.at(level));
    if (!action)
    {
      QStringList entries;
      for (QAction *a : popup->actions())
        if (!a->isSeparator())
          entries << plain(a->text());
      failure = QStringLiteral("no entry \"%1\" in [%2]").arg(path.at(level), entries.join(QStringLiteral(", ")));
      popup->close();
      while (auto *open = qobject_cast<QMenu *>(QApplication::activePopupWidget()))
        open->close();
      return;
    }
    if (!action->isEnabled())
    {
      failure = QStringLiteral("entry \"%1\" is disabled").arg(path.at(level));
      while (auto *open = qobject_cast<QMenu *>(QApplication::activePopupWidget()))
        open->close();
      return;
    }
    popup->setActiveAction(action);
    if (action->menu())
    {
      ++level;
      QTest::keyClick(popup, Qt::Key_Right); // opens the submenu as a child popup
      return;
    }
    failure.clear();
    driver.stop();
    // What QMenu does on a click: close the menu chain, then trigger.
    QPointer<QAction> chosen = action;
    while (auto *open = qobject_cast<QMenu *>(QApplication::activePopupWidget()))
      open->hide();
    if (chosen)
      chosen->trigger();
  });
  driver.start();
  QTimer watchdog;
  watchdog.setSingleShot(true);
  connect(&watchdog, &QTimer::timeout, this, [&] {
    if (failure.isEmpty())
      return;
    failure += QStringLiteral(" (menu watchdog)");
    while (auto *open = qobject_cast<QMenu *>(QApplication::activePopupWidget()))
      open->close();
  });
  watchdog.start(4000);
  QContextMenuEvent event(QContextMenuEvent::Mouse, pos, view->viewport()->mapToGlobal(pos));
  QCoreApplication::sendEvent(view->viewport(), &event);
  driver.stop();
  QTest::qWait(20);
  return failure;
}

QStringList MainWindowActionsTest::menuEntries(FilePanel *panel, const QString &rowName, const QString &submenu)
{
  QStringList entries;
  QTableView *view = panel->view();
  const int row = rowOf(panel, rowName);
  if (row < 0)
    return entries;
  const QPoint pos = view->visualRect(view->model()->index(row, 1)).center();
  QTest::mousePress(view->viewport(), Qt::RightButton, {}, pos);
  QTest::mouseRelease(view->viewport(), Qt::RightButton, {}, pos);
  QTimer driver;
  driver.setInterval(10);
  connect(&driver, &QTimer::timeout, this, [&] {
    auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    if (!popup)
      return;
    QMenu *target = popup;
    if (!submenu.isEmpty())
      if (QAction *sub = findAction(popup, submenu))
        target = sub->menu();
    if (target)
      for (QAction *a : target->actions())
        if (!a->isSeparator())
          entries << plain(a->text()) + (a->isEnabled() ? QString() : QStringLiteral(" [off]"));
    driver.stop();
    popup->close();
  });
  driver.start();
  QContextMenuEvent event(QContextMenuEvent::Mouse, pos, view->viewport()->mapToGlobal(pos));
  QCoreApplication::sendEvent(view->viewport(), &event);
  driver.stop();
  return entries;
}

QString MainWindowActionsTest::makeArchive(const QString &path, const QStringList &inputs)
{
  CompressOptions options;
  options.archivePath = path;
  options.formatIndex = SevenZipEngine::instance().formatIndex(
      QFileInfo(path).suffix() == QLatin1String("zip") ? QStringLiteral("zip") : QStringLiteral("7z"));
  options.level = 1;
  PromptDispatcher prompts;
  const OperationResult result = compressFiles(inputs, options, nullptr, &prompts);
  return result.ok ? path : QString();
}

QStringList MainWindowActionsTest::archiveNames(const QString &archive)
{
  ArchiveSession session;
  QString error;
  PromptDispatcher prompts;
  if (!session.open(archive, QString(), nullptr, &prompts, &error))
    return {QStringLiteral("<cannot open: %1>").arg(error)};
  if (!session.setFlatMode(true))
    return {QStringLiteral("<no flat mode>")};
  QStringList names;
  for (int i = 0; i < session.itemCount(); ++i)
  {
    const ArchiveEntryInfo item = session.item(i);
    QString name = item.path.isEmpty() ? item.name : item.path;
    while (name.endsWith(QLatin1Char('/')))
      name.chop(1);
    names << name;
  }
  names.sort();
  return names;
}

void MainWindowActionsTest::finishScript()
{
  if (!QTest::qWaitFor([this] { return m_script->done(); }, 15000))
    qWarning("Dialogs seen so far: %s", qPrintable(m_script->seen.join(QStringLiteral(" || "))));
  QVERIFY(m_script->done());
  QVERIFY2(m_script->unexpected.isEmpty(), qPrintable(m_script->unexpected.join(QStringLiteral(" || "))));
}

// --- local folder ---------------------------------------------------------

void MainWindowActionsTest::quickCompressKeepsWholeSelection()
{
  const QString dir = work(QStringLiteral("Downloads"));
  for (const QString &name : {QStringLiteral("a.deb"), QStringLiteral("b.deb"), QStringLiteral("c.txt")})
    QVERIFY(writeFile(dir + QLatin1Char('/') + name, name.toUtf8()));
  openFolder(left(), dir);
  select(left(), {QStringLiteral("a.deb"), QStringLiteral("b.deb")});

  // The menu names the archive after the folder and the click keeps both rows.
  const QStringList entries = menuEntries(left(), QStringLiteral("b.deb"), QStringLiteral("7-Zip"));
  QVERIFY2(entries.contains(QStringLiteral("Add to \"Downloads.7z\"")), qPrintable(entries.join(QStringLiteral(", "))));
  QCOMPARE(left()->selectedLocalPaths().size(), 2);

  QCOMPARE(menu(left(), QStringLiteral("b.deb"), {QStringLiteral("7-Zip"), QStringLiteral("Add to \"Downloads.7z\"")}),
           QString());
  finishScript();
  const QString archive = dir + QStringLiteral("/Downloads.7z");
  QVERIFY(QFileInfo::exists(archive));
  QCOMPARE(archiveNames(archive), (QStringList{QStringLiteral("a.deb"), QStringLiteral("b.deb")}));
  waitLocal(left());
  QCOMPARE(left()->selectedLocalPaths(), QStringList{archive});

  // A second run asks before adding to the existing archive.
  select(left(), {QStringLiteral("c.txt"), QStringLiteral("a.deb")});
  m_script->expect(QStringLiteral("update"), [](QWidget *d) { buttonNamed(d, QStringLiteral("Yes"))->click(); });
  QCOMPARE(menu(left(), QStringLiteral("c.txt"), {QStringLiteral("7-Zip"), QStringLiteral("Add to \"Downloads.7z\"")}),
           QString());
  finishScript();
  QVERIFY(m_script->seen.first().contains(QStringLiteral("Add the selected item(s) to it?")));
  QCOMPARE(archiveNames(archive),
           (QStringList{QStringLiteral("a.deb"), QStringLiteral("b.deb"), QStringLiteral("c.txt")}));
}

void MainWindowActionsTest::quickZipSingleFile()
{
  QVERIFY(writeFile(work(QStringLiteral("notes.txt")), "notes"));
  openFolder(left(), work());
  select(left(), {QStringLiteral("notes.txt")});
  QCOMPARE(menu(left(), QStringLiteral("notes.txt"), {QStringLiteral("7-Zip"), QStringLiteral("Add to \"notes.zip\"")}),
           QString());
  finishScript();
  QCOMPARE(archiveNames(work(QStringLiteral("notes.zip"))), QStringList{QStringLiteral("notes.txt")});
}

void MainWindowActionsTest::addToArchiveDialog()
{
  QVERIFY(writeFile(work(QStringLiteral("src/one.txt")), "1"));
  QVERIFY(writeFile(work(QStringLiteral("src/two.txt")), "2"));
  openFolder(left(), work(QStringLiteral("src")));
  select(left(), {QStringLiteral("one.txt"), QStringLiteral("two.txt")});
  QString proposed;
  m_script->expect(QStringLiteral("compress"), [&proposed](QWidget *d) {
    QVERIFY(qobject_cast<CompressDialog *>(d));
    auto *path = d->findChildren<QLineEdit *>().first();
    proposed = path->text();
    auto *format = d->findChildren<QComboBox *>().first();
    format->setCurrentIndex(format->findText(QStringLiteral("zip"), Qt::MatchStartsWith));
    clickAccept(d);
  });
  QCOMPARE(menu(left(), QStringLiteral("one.txt"), {QStringLiteral("7-Zip"), QStringLiteral("Add to archive...")}), QString());
  finishScript();
  QCOMPARE(proposed, work(QStringLiteral("src/src.7z")));
  QCOMPARE(archiveNames(work(QStringLiteral("src/src.zip"))),
           (QStringList{QStringLiteral("one.txt"), QStringLiteral("two.txt")}));
}

void MainWindowActionsTest::extractHereFromMenu()
{
  QVERIFY(writeFile(work(QStringLiteral("in/hello.txt")), "hello"));
  const QString archive = makeArchive(work(QStringLiteral("pack.zip")), {work(QStringLiteral("in/hello.txt"))});
  QVERIFY(!archive.isEmpty());
  QDir(work(QStringLiteral("in"))).removeRecursively();
  openFolder(left(), work());
  QCOMPARE(menu(left(), QStringLiteral("pack.zip"), {QStringLiteral("7-Zip"), QStringLiteral("Extract Here")}), QString());
  finishScript();
  QCOMPARE(readFile(work(QStringLiteral("hello.txt"))), QByteArray("hello"));
  waitLocal(left());
  QVERIFY(rowOf(left(), QStringLiteral("hello.txt")) >= 0);
}

void MainWindowActionsTest::extractToFolderFromMenu()
{
  QVERIFY(writeFile(work(QStringLiteral("in/a.txt")), "a"));
  QVERIFY(!makeArchive(work(QStringLiteral("photos.7z")), {work(QStringLiteral("in/a.txt"))}).isEmpty());
  openFolder(left(), work());
  QCOMPARE(menu(left(), QStringLiteral("photos.7z"), {QStringLiteral("7-Zip"), QStringLiteral("Extract to \"photos/\"")}),
           QString());
  finishScript();
  QCOMPARE(readFile(work(QStringLiteral("photos/a.txt"))), QByteArray("a"));
}

void MainWindowActionsTest::extractFilesDialogFromMenu()
{
  QVERIFY(writeFile(work(QStringLiteral("in/a.txt")), "a"));
  QVERIFY(!makeArchive(work(QStringLiteral("docs.7z")), {work(QStringLiteral("in/a.txt"))}).isEmpty());
  openFolder(left(), work());
  QString proposed;
  const QString target = work(QStringLiteral("chosen"));
  m_script->expect(QStringLiteral("extract"), [&proposed, target](QWidget *d) {
    QVERIFY(qobject_cast<ExtractDialog *>(d));
    QLineEdit *dir = nullptr;
    for (QLineEdit *edit : d->findChildren<QLineEdit *>())
      if (edit->echoMode() == QLineEdit::Normal && !dir)
        dir = edit;
    proposed = dir->text();
    dir->setText(target);
    clickAccept(d);
  });
  QCOMPARE(menu(left(), QStringLiteral("docs.7z"), {QStringLiteral("7-Zip"), QStringLiteral("Extract files...")}), QString());
  finishScript();
  QCOMPARE(proposed, work(QStringLiteral("docs")));
  QCOMPARE(readFile(target + QStringLiteral("/a.txt")), QByteArray("a"));
}

void MainWindowActionsTest::extractEachToOwnFolder()
{
  QVERIFY(writeFile(work(QStringLiteral("in/a.txt")), "a"));
  QVERIFY(writeFile(work(QStringLiteral("in/b.txt")), "b"));
  QVERIFY(!makeArchive(work(QStringLiteral("first.7z")), {work(QStringLiteral("in/a.txt"))}).isEmpty());
  QVERIFY(!makeArchive(work(QStringLiteral("second.tar.gz.zip")), {work(QStringLiteral("in/b.txt"))}).isEmpty());
  openFolder(left(), work());
  select(left(), {QStringLiteral("first.7z"), QStringLiteral("second.tar.gz.zip")});
  QCOMPARE(menu(left(), QStringLiteral("first.7z"), {QStringLiteral("7-Zip"), QStringLiteral("Extract each to its own folder")}),
           QString());
  finishScript();
  QCOMPARE(readFile(work(QStringLiteral("first/a.txt"))), QByteArray("a"));
  QCOMPARE(readFile(work(QStringLiteral("second/b.txt"))), QByteArray("b"));
}

void MainWindowActionsTest::testArchiveFromMenu()
{
  QVERIFY(writeFile(work(QStringLiteral("in/a.txt")), "a"));
  QVERIFY(!makeArchive(work(QStringLiteral("ok.7z")), {work(QStringLiteral("in/a.txt"))}).isEmpty());
  QVERIFY(writeFile(work(QStringLiteral("broken.7z")), "this is not an archive"));
  openFolder(left(), work());

  m_script->expect(QStringLiteral("result"), [](QWidget *d) { clickAccept(d); });
  QCOMPARE(menu(left(), QStringLiteral("ok.7z"), {QStringLiteral("7-Zip"), QStringLiteral("Test archive")}), QString());
  finishScript();
  QVERIFY2(m_script->seen.last().contains(QStringLiteral("No errors")), qPrintable(m_script->seen.last()));

  m_script->reset();
  m_script->expect(QStringLiteral("error"), [](QWidget *d) { clickAccept(d); });
  QCOMPARE(menu(left(), QStringLiteral("broken.7z"), {QStringLiteral("7-Zip"), QStringLiteral("Test archive")}), QString());
  finishScript();
  QVERIFY2(m_script->seen.last().contains(QStringLiteral("broken.7z")), qPrintable(m_script->seen.last()));
}

void MainWindowActionsTest::openArchiveFromMenuAndGoUp()
{
  QVERIFY(writeFile(work(QStringLiteral("in/a.txt")), "a"));
  const QString archive = makeArchive(work(QStringLiteral("box.7z")), {work(QStringLiteral("in/a.txt"))});
  openFolder(left(), work());
  QCOMPARE(menu(left(), QStringLiteral("box.7z"), {QStringLiteral("7-Zip"), QStringLiteral("Open archive")}), QString());
  finishScript();
  QVERIFY(left()->isArchive());
  QVERIFY(rowOf(left(), QStringLiteral("a.txt")) >= 0);
  // View > Up One Level (Alt+Up) leaves the archive.
  QAction *up = nullptr;
  for (QAction *a : window()->findChildren<QAction *>())
    if (plain(a->text()) == QLatin1String("Up One Level"))
      up = a;
  QVERIFY(up);
  up->trigger();
  QVERIFY(!left()->isArchive());
  waitLocal(left());
  QCOMPARE(left()->selectedLocalPaths(), QStringList{archive});
}

void MainWindowActionsTest::debPackagesAreArchives()
{
  QVERIFY(writeFile(work(QStringLiteral("tool_1.0_amd64.deb")), "!<arch>\n"));
  openFolder(left(), work());
  const QStringList entries = menuEntries(left(), QStringLiteral("tool_1.0_amd64.deb"), QStringLiteral("7-Zip"));
  QVERIFY2(entries.contains(QStringLiteral("Extract Here")), qPrintable(entries.join(QStringLiteral(", "))));
  QVERIFY2(entries.contains(QStringLiteral("Extract to \"tool_1.0_amd64/\"")), qPrintable(entries.join(QStringLiteral(", "))));
}

void MainWindowActionsTest::renameLocalItem()
{
  QVERIFY(writeFile(work(QStringLiteral("old.txt")), "keep"));
  openFolder(left(), work());
  m_script->expect(QStringLiteral("rename"), [](QWidget *d) {
    d->findChildren<QLineEdit *>().first()->setText(QStringLiteral("new.txt"));
    clickAccept(d);
  });
  QCOMPARE(menu(left(), QStringLiteral("old.txt"), {QStringLiteral("Rename...")}), QString());
  finishScript();
  QCOMPARE(readFile(work(QStringLiteral("new.txt"))), QByteArray("keep"));
  QVERIFY(!QFileInfo::exists(work(QStringLiteral("old.txt"))));
  waitLocal(left());
  QCOMPARE(left()->selectedLocalPaths(), QStringList{work(QStringLiteral("new.txt"))});
}

void MainWindowActionsTest::renameRejectsSlash()
{
  QVERIFY(writeFile(work(QStringLiteral("old.txt")), "keep"));
  QDir().mkpath(work(QStringLiteral("elsewhere")));
  openFolder(left(), work());
  m_script->expect(QStringLiteral("rename"), [](QWidget *d) {
    d->findChildren<QLineEdit *>().first()->setText(QStringLiteral("elsewhere/moved.txt"));
    clickAccept(d);
  });
  m_script->expect(QStringLiteral("error"), [](QWidget *d) { clickAccept(d); });
  QCOMPARE(menu(left(), QStringLiteral("old.txt"), {QStringLiteral("Rename...")}), QString());
  finishScript();
  QVERIFY(QFileInfo::exists(work(QStringLiteral("old.txt"))));
  QVERIFY(!QFileInfo::exists(work(QStringLiteral("elsewhere/moved.txt"))));
}

void MainWindowActionsTest::createFolderAndFile()
{
  openFolder(left(), work());
  QVERIFY(writeFile(work(QStringLiteral("anchor.txt")), "x"));
  left()->refresh();
  waitLocal(left());
  m_script->expect(QStringLiteral("folder"), [](QWidget *d) {
    d->findChildren<QLineEdit *>().first()->setText(QStringLiteral("Reports"));
    clickAccept(d);
  });
  QCOMPARE(menu(left(), QStringLiteral("anchor.txt"), {QStringLiteral("Create Folder...")}), QString());
  finishScript();
  QVERIFY(QFileInfo(work(QStringLiteral("Reports"))).isDir());
  waitLocal(left());
  QCOMPARE(left()->selectedLocalPaths(), QStringList{work(QStringLiteral("Reports"))});

  m_script->expect(QStringLiteral("file"), [](QWidget *d) {
    d->findChildren<QLineEdit *>().first()->setText(QStringLiteral("todo.md"));
    clickAccept(d);
  });
  QCOMPARE(menu(left(), QStringLiteral("anchor.txt"), {QStringLiteral("Create File...")}), QString());
  finishScript();
  QVERIFY(QFileInfo(work(QStringLiteral("todo.md"))).isFile());
}

void MainWindowActionsTest::createFileNeverTruncates()
{
  QVERIFY(writeFile(work(QStringLiteral("important.txt")), "precious data"));
  openFolder(left(), work());
  m_script->expect(QStringLiteral("file"), [](QWidget *d) {
    d->findChildren<QLineEdit *>().first()->setText(QStringLiteral("important.txt"));
    clickAccept(d);
  });
  m_script->expect(QStringLiteral("error"), [](QWidget *d) { clickAccept(d); });
  QCOMPARE(menu(left(), QStringLiteral("important.txt"), {QStringLiteral("Create File...")}), QString());
  finishScript();
  QCOMPARE(readFile(work(QStringLiteral("important.txt"))), QByteArray("precious data"));
  QVERIFY(m_script->seen.last().contains(QStringLiteral("already exists")));
}

void MainWindowActionsTest::deleteMovesToTrash()
{
  const QString name = QStringLiteral("trash-me-%1.txt").arg(QCoreApplication::applicationPid());
  QVERIFY(writeFile(work(name), "bye"));
  QVERIFY(writeFile(work(QStringLiteral("stay.txt")), "stay"));
  openFolder(left(), work());
  m_script->expect(QStringLiteral("confirm"), [](QWidget *d) { buttonNamed(d, QStringLiteral("Yes"))->click(); });
  QCOMPARE(menu(left(), name, {QStringLiteral("Move to Trash")}), QString());
  // Either the trash took it, or the app asked before deleting permanently.
  QTest::qWait(200);
  if (QFileInfo::exists(work(name)))
  {
    m_script->expect(QStringLiteral("permanent"), [](QWidget *d) { buttonNamed(d, QStringLiteral("Yes"))->click(); });
    QTest::qWait(200);
  }
  finishScript();
  QVERIFY(!QFileInfo::exists(work(name)));
  QVERIFY(QFileInfo::exists(work(QStringLiteral("stay.txt"))));
  // Remove what this test put in the trash.
  for (const QString &trash : {QDir::homePath() + QStringLiteral("/.local/share/Trash"),
                               QStringLiteral("/tmp/.Trash-%1").arg(getuid())})
  {
    QFile::remove(trash + QStringLiteral("/files/") + name);
    QFile::remove(trash + QStringLiteral("/info/") + name + QStringLiteral(".trashinfo"));
  }
}

void MainWindowActionsTest::splitAndCombine()
{
  QByteArray data;
  for (int i = 0; i < 3 * 1024 * 1024 + 123; ++i)
    data.append(char('a' + i % 26));
  QVERIFY(writeFile(work(QStringLiteral("big.bin")), data));
  openFolder(left(), work());
  m_script->expect(QStringLiteral("size"), [](QWidget *d) {
    d->findChildren<QSpinBox *>().first()->setValue(1);
    clickAccept(d);
  });
  QCOMPARE(menu(left(), QStringLiteral("big.bin"), {QStringLiteral("More"), QStringLiteral("Split file...")}), QString());
  finishScript();
  for (const QString &part : {QStringLiteral("001"), QStringLiteral("002"), QStringLiteral("003"), QStringLiteral("004")})
    QVERIFY2(QFileInfo::exists(work(QStringLiteral("big.bin.") + part)), qPrintable(part));
  QVERIFY(!QFileInfo::exists(work(QStringLiteral("big.bin.005"))));
  QCOMPARE(QFileInfo(work(QStringLiteral("big.bin.004"))).size(), qint64(123));

  // Splitting again refuses to overwrite the volumes.
  waitLocal(left());
  m_script->expect(QStringLiteral("size"), [](QWidget *d) {
    d->findChildren<QSpinBox *>().first()->setValue(1);
    clickAccept(d);
  });
  m_script->expect(QStringLiteral("error"), [](QWidget *d) { clickAccept(d); });
  QCOMPARE(menu(left(), QStringLiteral("big.bin"), {QStringLiteral("More"), QStringLiteral("Split file...")}), QString());
  finishScript();

  waitLocal(left());
  m_script->expect(QStringLiteral("output"), [this](QWidget *d) {
    d->findChildren<QLineEdit *>().first()->setText(work(QStringLiteral("joined.bin")));
    clickAccept(d);
  });
  QCOMPARE(menu(left(), QStringLiteral("big.bin.001"), {QStringLiteral("More"), QStringLiteral("Combine files...")}), QString());
  finishScript();
  QCOMPARE(readFile(work(QStringLiteral("joined.bin"))), data);
}

void MainWindowActionsTest::propertiesAreReadable()
{
  QVERIFY(writeFile(work(QStringLiteral("doc.txt")), QByteArray(2048, 'x')));
  QFile::setPermissions(work(QStringLiteral("doc.txt")),
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup |
                            QFileDevice::ReadOther);
  openFolder(left(), work());
  m_script->expect(QStringLiteral("props"), [](QWidget *d) { clickAccept(d); });
  QCOMPARE(menu(left(), QStringLiteral("doc.txt"), {QStringLiteral("Properties")}), QString());
  finishScript();
  const QString text = m_script->seen.last();
  QVERIFY2(text.contains(QStringLiteral("Permissions: -rw-r--r--")), qPrintable(text));
  QVERIFY2(text.contains(QStringLiteral("2,048 bytes")) || text.contains(QStringLiteral("2048 bytes")), qPrintable(text));
}

void MainWindowActionsTest::hashSelection()
{
  QVERIFY(writeFile(work(QStringLiteral("abc.txt")), "abc"));
  openFolder(left(), work());
  m_script->expect(QStringLiteral("hash"), [](QWidget *d) {
    QVERIFY(qobject_cast<HashResultDialog *>(d));
    qobject_cast<QDialog *>(d)->accept();
  });
  QCOMPARE(menu(left(), QStringLiteral("abc.txt"), {QStringLiteral("CRC"), QStringLiteral("SHA256")}), QString());
  finishScript();
  QVERIFY2(m_script->seen.last().contains(
               QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), Qt::CaseInsensitive),
           qPrintable(m_script->seen.last()));
}

void MainWindowActionsTest::copyAndMoveToOtherPanel()
{
  QVERIFY(writeFile(work(QStringLiteral("src/a.txt")), "a"));
  QVERIFY(writeFile(work(QStringLiteral("src/b.txt")), "b"));
  QDir().mkpath(work(QStringLiteral("dst")));
  window()->setTwoPanels(true);
  openFolder(left(), work(QStringLiteral("src")));
  openFolder(right(), work(QStringLiteral("dst")));
  left()->focusView();
  select(left(), {QStringLiteral("a.txt"), QStringLiteral("b.txt")});
  QCOMPARE(menu(left(), QStringLiteral("a.txt"), {QStringLiteral("Copy to Other Panel")}), QString());
  finishScript();
  QCOMPARE(readFile(work(QStringLiteral("dst/a.txt"))), QByteArray("a"));
  QCOMPARE(readFile(work(QStringLiteral("dst/b.txt"))), QByteArray("b"));
  QVERIFY(QFileInfo::exists(work(QStringLiteral("src/a.txt"))));

  waitLocal(left());
  QVERIFY(writeFile(work(QStringLiteral("src/c.txt")), "c"));
  left()->refresh();
  waitLocal(left());
  select(left(), {QStringLiteral("c.txt")});
  m_script->expect(QStringLiteral("confirm move"), [](QWidget *d) { buttonNamed(d, QStringLiteral("Yes"))->click(); });
  QCOMPARE(menu(left(), QStringLiteral("c.txt"), {QStringLiteral("Move to Other Panel")}), QString());
  finishScript();
  QCOMPARE(readFile(work(QStringLiteral("dst/c.txt"))), QByteArray("c"));
  QVERIFY(!QFileInfo::exists(work(QStringLiteral("src/c.txt"))));
}

void MainWindowActionsTest::createSymbolicLink()
{
  QVERIFY(writeFile(work(QStringLiteral("target.txt")), "t"));
  openFolder(left(), work());
  m_script->expect(QStringLiteral("type"), [](QWidget *d) { clickAccept(d); });
  m_script->expect(QStringLiteral("path"), [this](QWidget *d) {
    d->findChildren<QLineEdit *>().first()->setText(work(QStringLiteral("shortcut")));
    clickAccept(d);
  });
  QCOMPARE(menu(left(), QStringLiteral("target.txt"), {QStringLiteral("More"), QStringLiteral("Link...")}), QString());
  finishScript();
  QVERIFY(QFileInfo(work(QStringLiteral("shortcut"))).isSymLink());
  QCOMPARE(QFileInfo(work(QStringLiteral("shortcut"))).symLinkTarget(), work(QStringLiteral("target.txt")));
}

void MainWindowActionsTest::selectAllAndInvert()
{
  for (const QString &name : {QStringLiteral("1.txt"), QStringLiteral("2.txt"), QStringLiteral("3.txt")})
    QVERIFY(writeFile(work(name), "x"));
  openFolder(left(), work());
  select(left(), {QStringLiteral("1.txt")});
  QCOMPARE(menu(left(), QStringLiteral("1.txt"), {QStringLiteral("Select All")}), QString());
  QCOMPARE(left()->selectedLocalPaths().size(), 3);
  select(left(), {QStringLiteral("1.txt")});
  QCOMPARE(menu(left(), QStringLiteral("1.txt"), {QStringLiteral("Invert Selection")}), QString());
  QStringList selected = left()->selectedLocalPaths();
  selected.sort();
  QCOMPARE(selected, (QStringList{work(QStringLiteral("2.txt")), work(QStringLiteral("3.txt"))}));
}

// --- inside an archive ------------------------------------------------------

static QString demoArchive(const QString &dir, const QString &name)
{
  writeFile(dir + QStringLiteral("/payload/docs/readme.txt"), "readme");
  writeFile(dir + QStringLiteral("/payload/docs/guide.txt"), "guide");
  writeFile(dir + QStringLiteral("/payload/top.txt"), "top");
  CompressOptions options;
  options.archivePath = dir + QLatin1Char('/') + name;
  options.formatIndex = SevenZipEngine::instance().formatIndex(
      QFileInfo(name).suffix() == QLatin1String("zip") ? QStringLiteral("zip") : QStringLiteral("7z"));
  PromptDispatcher prompts;
  const OperationResult result = compressFiles(
      {dir + QStringLiteral("/payload/docs"), dir + QStringLiteral("/payload/top.txt")}, options, nullptr, &prompts);
  QDir(dir + QStringLiteral("/payload")).removeRecursively();
  return result.ok ? options.archivePath : QString();
}

void MainWindowActionsTest::archiveExtractHereFromSubfolder()
{
  const QString archive = demoArchive(work(), QStringLiteral("demo.7z"));
  QVERIFY(!archive.isEmpty());
  window()->openPath(archive);
  QVERIFY(left()->isArchive());
  QVERIFY(left()->enterArchivePath(QStringLiteral("docs")));
  select(left(), {QStringLiteral("readme.txt")});
  QCOMPARE(menu(left(), QStringLiteral("readme.txt"), {QStringLiteral("Extract Here")}), QString());
  finishScript();
  QVERIFY2(QFileInfo::exists(work(QStringLiteral("docs/readme.txt"))) || QFileInfo::exists(work(QStringLiteral("readme.txt"))),
           "extracted next to the archive");
}

void MainWindowActionsTest::archiveExtractToFolder()
{
  const QString archive = demoArchive(work(), QStringLiteral("demo.7z"));
  window()->openPath(archive);
  QCOMPARE(menu(left(), QStringLiteral("top.txt"), {QStringLiteral("Extract to \"demo/\"")}), QString());
  finishScript();
  QCOMPARE(readFile(work(QStringLiteral("demo/top.txt"))), QByteArray("top"));
}

void MainWindowActionsTest::archiveExtractDialogDefaultsNextToArchive()
{
  const QString archive = demoArchive(work(), QStringLiteral("demo.7z"));
  window()->openPath(archive);
  QVERIFY(left()->enterArchivePath(QStringLiteral("docs")));
  QString proposed;
  m_script->expect(QStringLiteral("extract"), [&proposed](QWidget *d) {
    proposed = d->findChildren<QLineEdit *>().first()->text();
    qobject_cast<QDialog *>(d)->reject();
  });
  QCOMPARE(menu(left(), QStringLiteral("readme.txt"), {QStringLiteral("Extract files...")}), QString());
  finishScript();
  // Not ".../demo.7z" (the archive file) just because we are inside docs/.
  QCOMPARE(proposed, work());
}

void MainWindowActionsTest::archiveTestFromMenu()
{
  const QString archive = demoArchive(work(), QStringLiteral("demo.7z"));
  window()->openPath(archive);
  m_script->expect(QStringLiteral("ok"), [](QWidget *d) { clickAccept(d); });
  QCOMPARE(menu(left(), QStringLiteral("top.txt"), {QStringLiteral("Test archive")}), QString());
  finishScript();
  QVERIFY(m_script->seen.last().contains(QStringLiteral("No errors")));
}

void MainWindowActionsTest::archiveRenameDeleteCreateFolder()
{
  const QString archive = demoArchive(work(), QStringLiteral("demo.7z"));
  window()->openPath(archive);

  m_script->expect(QStringLiteral("rename"), [](QWidget *d) {
    d->findChildren<QLineEdit *>().first()->setText(QStringLiteral("renamed.txt"));
    clickAccept(d);
  });
  QCOMPARE(menu(left(), QStringLiteral("top.txt"), {QStringLiteral("Rename...")}), QString());
  finishScript();
  QVERIFY(archiveNames(archive).contains(QStringLiteral("renamed.txt")));
  QVERIFY(rowOf(left(), QStringLiteral("renamed.txt")) >= 0);

  m_script->expect(QStringLiteral("folder"), [](QWidget *d) {
    d->findChildren<QLineEdit *>().first()->setText(QStringLiteral("empty"));
    clickAccept(d);
  });
  QCOMPARE(menu(left(), QStringLiteral("renamed.txt"), {QStringLiteral("Create Folder...")}), QString());
  finishScript();
  QVERIFY(archiveNames(archive).contains(QStringLiteral("empty")));

  m_script->expect(QStringLiteral("confirm"), [](QWidget *d) { buttonNamed(d, QStringLiteral("Yes"))->click(); });
  QCOMPARE(menu(left(), QStringLiteral("renamed.txt"), {QStringLiteral("Delete")}), QString());
  finishScript();
  QVERIFY(!archiveNames(archive).contains(QStringLiteral("renamed.txt")));
  QVERIFY(archiveNames(archive).contains(QStringLiteral("docs/readme.txt")));
}

void MainWindowActionsTest::archiveCommentZip()
{
  const QString archive = demoArchive(work(), QStringLiteral("demo.zip"));
  window()->openPath(archive);
  m_script->expect(QStringLiteral("comment"), [](QWidget *d) {
    d->findChildren<QPlainTextEdit *>().first()->setPlainText(QStringLiteral("hello comment"));
    clickAccept(d);
  });
  QCOMPARE(menu(left(), QStringLiteral("top.txt"), {QStringLiteral("Comment...")}), QString());
  finishScript();
  const int item = left()->archiveModel()->itemForRow(rowOf(left(), QStringLiteral("top.txt")));
  QCOMPARE(left()->archiveModel()->entry(item).comment, QStringLiteral("hello comment"));
}

void MainWindowActionsTest::archiveProperties()
{
  const QString archive = demoArchive(work(), QStringLiteral("demo.7z"));
  window()->openPath(archive);
  m_script->expect(QStringLiteral("props"), [](QWidget *d) { clickAccept(d); });
  QCOMPARE(menu(left(), QStringLiteral("top.txt"), {QStringLiteral("Properties")}), QString());
  finishScript();
  QVERIFY2(m_script->seen.last().contains(QStringLiteral("Archive: ") + archive), qPrintable(m_script->seen.last()));
}

void MainWindowActionsTest::archiveCopyToOtherPanel()
{
  const QString archive = demoArchive(work(), QStringLiteral("demo.7z"));
  QDir().mkpath(work(QStringLiteral("out")));
  window()->setTwoPanels(true);
  openFolder(right(), work(QStringLiteral("out")));
  left()->focusView();
  window()->openPath(archive);
  QVERIFY(left()->isArchive());
  select(left(), {QStringLiteral("docs"), QStringLiteral("top.txt")});
  QCOMPARE(menu(left(), QStringLiteral("top.txt"), {QStringLiteral("Copy to Other Panel")}), QString());
  finishScript();
  QCOMPARE(readFile(work(QStringLiteral("out/top.txt"))), QByteArray("top"));
  QCOMPARE(readFile(work(QStringLiteral("out/docs/readme.txt"))), QByteArray("readme"));
}

void MainWindowActionsTest::nestedArchiveExtractHere()
{
  QVERIFY(writeFile(work(QStringLiteral("inner/deep.txt")), "deep"));
  const QString inner = makeArchive(work(QStringLiteral("inner.zip")), {work(QStringLiteral("inner/deep.txt"))});
  const QString outer = makeArchive(work(QStringLiteral("outer.7z")), {inner});
  QFile::remove(inner);
  QDir(work(QStringLiteral("inner"))).removeRecursively();
  window()->openPath(outer);
  const int innerItem = left()->archiveModel()->itemForRow(rowOf(left(), QStringLiteral("inner.zip")));
  QString error;
  PromptDispatcher prompts;
  QVERIFY2(left()->openNestedArchive(innerItem, nullptr, &prompts, &error), qPrintable(error));
  QCOMPARE(menu(left(), QStringLiteral("deep.txt"), {QStringLiteral("Extract Here")}), QString());
  finishScript();
  // Next to outer.7z, not in the nested archive's temporary folder.
  QCOMPARE(readFile(work(QStringLiteral("deep.txt"))), QByteArray("deep"));
}

// --- toolbar ---------------------------------------------------------------

void MainWindowActionsTest::toolbarActionsFollowMode()
{
  const QString archive = demoArchive(work(), QStringLiteral("demo.7z"));
  openFolder(left(), work());
  auto action = [this](const QString &text) -> QAction * {
    auto *toolbar = window()->findChild<QToolBar *>();
    QList<QAction *> actions = toolbar->actions();
    for (QToolButton *button : toolbar->findChildren<QToolButton *>())
      if (button->defaultAction())
        actions.append(button->defaultAction());
    for (QAction *a : std::as_const(actions))
      if (plain(a->text()).startsWith(text))
        return a;
    return nullptr;
  };
  QVERIFY(action(QStringLiteral("Add")));
  QVERIFY(action(QStringLiteral("Add"))->isEnabled());
  QVERIFY(!action(QStringLiteral("Extract"))->isEnabled());
  window()->openPath(archive);
  QVERIFY(!action(QStringLiteral("Add"))->isEnabled());
  QVERIFY(action(QStringLiteral("Extract"))->isEnabled());
  QVERIFY(action(QStringLiteral("Test"))->isEnabled());
  // Leaving through the ".." row updates the toolbar too.
  emit left()->view()->activated(left()->view()->model()->index(0, 0));
  QVERIFY(!left()->isArchive());
  QVERIFY(action(QStringLiteral("Add"))->isEnabled());
  QVERIFY(!action(QStringLiteral("Extract"))->isEnabled());
}

void MainWindowActionsTest::clipboardCopyPaste()
{
  QVERIFY(writeFile(work(QStringLiteral("src/clip.txt")), "clip"));
  QDir().mkpath(work(QStringLiteral("dst")));
  openFolder(left(), work(QStringLiteral("src")));
  select(left(), {QStringLiteral("clip.txt")});
  QMetaObject::invokeMethod(window(), "clipboardCopy");
  QVERIFY(QApplication::clipboard()->mimeData()->hasUrls());
  openFolder(left(), work(QStringLiteral("dst")));
  QMetaObject::invokeMethod(window(), "clipboardPaste");
  finishScript();
  QCOMPARE(readFile(work(QStringLiteral("dst/clip.txt"))), QByteArray("clip"));
}

void MainWindowActionsTest::archiveViewMaterializesItem()
{
  const QString archive = demoArchive(work(), QStringLiteral("demo.7z"));
  const QString seen = work(QStringLiteral("viewer-saw.txt"));
  QSettings().setValue(QStringLiteral("tools/viewerCommand"),
                       QStringLiteral("sh -c 'cat \"$0\" > \"%1\"'").arg(seen));
  window()->openPath(archive);
  QCOMPARE(menu(left(), QStringLiteral("top.txt"), {QStringLiteral("View")}), QString());
  finishScript();
  QTRY_COMPARE_WITH_TIMEOUT(readFile(seen), QByteArray("top"), 5000);
}

void MainWindowActionsTest::archiveEditWritesBack()
{
  const QString archive = demoArchive(work(), QStringLiteral("demo.7z"));
  QSettings().setValue(QStringLiteral("tools/editorCommand"),
                       QStringLiteral("sh -c 'sleep 0.3; printf edited > \"$0\"'"));
  window()->openPath(archive);
  m_script->expect(QStringLiteral("update archive"), [](QWidget *d) {
    buttonNamed(d, QStringLiteral("Update Archive"))->click();
  });
  QCOMPARE(menu(left(), QStringLiteral("top.txt"), {QStringLiteral("Edit")}), QString());
  QTRY_VERIFY_WITH_TIMEOUT(m_script->done(), 10000);
  QTest::qWait(300);
  finishScript();
  QTemporaryDir out;
  ArchiveSession session;
  QString error;
  PromptDispatcher prompts;
  QVERIFY2(session.open(archive, QString(), nullptr, &prompts, &error), qPrintable(error));
  ExtractOptions options;
  options.outputDir = out.path();
  QVERIFY(session.extract({}, options, nullptr, &prompts).ok);
  QCOMPARE(readFile(out.path() + QStringLiteral("/top.txt")), QByteArray("edited"));
}

void MainWindowActionsTest::archiveFlatView()
{
  const QString archive = demoArchive(work(), QStringLiteral("demo.7z"));
  window()->openPath(archive);
  QAction *flat = nullptr;
  for (QAction *a : window()->findChildren<QAction *>())
    if (plain(a->text()) == QLatin1String("Flat View"))
      flat = a;
  QVERIFY(flat);
  flat->setChecked(true);
  QVERIFY(rowOf(left(), QStringLiteral("readme.txt")) >= 0);
  QVERIFY(rowOf(left(), QStringLiteral("top.txt")) >= 0);
  flat->setChecked(false);
  QVERIFY(rowOf(left(), QStringLiteral("readme.txt")) < 0);
  QVERIFY(rowOf(left(), QStringLiteral("docs")) >= 0);
}

void MainWindowActionsTest::toolsHashDialog()
{
  QVERIFY(writeFile(work(QStringLiteral("abc.txt")), "abc"));
  openFolder(left(), work());
  select(left(), {QStringLiteral("abc.txt")});
  m_script->expect(QStringLiteral("method"), [](QWidget *d) {
    auto *combo = d->findChildren<QComboBox *>().first();
    combo->setCurrentIndex(combo->findText(QStringLiteral("CRC32")));
    clickAccept(d);
  });
  m_script->expect(QStringLiteral("result"), [](QWidget *d) { qobject_cast<QDialog *>(d)->accept(); });
  QMetaObject::invokeMethod(window(), "calculateHash");
  finishScript();
  QVERIFY2(m_script->seen.last().contains(QStringLiteral("352441C2"), Qt::CaseInsensitive),
           qPrintable(m_script->seen.last()));
}

void MainWindowActionsTest::optionsDialogApplies()
{
  m_script->expect(QStringLiteral("options"), [](QWidget *d) {
    for (QCheckBox *box : d->findChildren<QCheckBox *>())
      if (plain(box->text()).startsWith(QStringLiteral("Show grid")))
        box->setChecked(true);
    clickAccept(d);
  });
  QMetaObject::invokeMethod(window(), "showOptions");
  finishScript();
  QVERIFY(left()->view()->showGrid());
  QCOMPARE(QSettings().value(QStringLiteral("view/showGrid")).toBool(), true);
}

void MainWindowActionsTest::favoritesAndComputer()
{
  openFolder(left(), work());
  QMetaObject::invokeMethod(window(), "addFavorite");
  QCOMPARE(QSettings().value(QStringLiteral("favorites")).toStringList(), QStringList{work()});
  QMetaObject::invokeMethod(window(), "showRoot");
  QCOMPARE(left()->mode(), FilePanel::RootMode);
  QVERIFY(left()->view()->model()->rowCount() > 0);
  emit left()->view()->activated(left()->view()->model()->index(0, 0));
  QCOMPARE(left()->mode(), FilePanel::LocalMode);
  QMetaObject::invokeMethod(window(), "clearFavorites");
  QVERIFY(QSettings().value(QStringLiteral("favorites")).toStringList().isEmpty());
}

void MainWindowActionsTest::benchmarkDialogOpens()
{
  m_script->expect(QStringLiteral("benchmark"), [](QWidget *d) { qobject_cast<QDialog *>(d)->reject(); });
  QMetaObject::invokeMethod(window(), "showBenchmark");
  finishScript();
}

void MainWindowActionsTest::filterBoxFiltersActivePanel()
{
  for (const QString &name : {QStringLiteral("alpha.deb"), QStringLiteral("beta.deb"), QStringLiteral("notes.txt")})
    QVERIFY(writeFile(work(name), "x"));
  openFolder(left(), work());
  auto *filter = window()->findChild<QLineEdit *>(QStringLiteral("toolbarFilter"));
  QVERIFY(filter);
  QTest::keyClick(window(), Qt::Key_F, Qt::ControlModifier);
  QVERIFY(filter->hasFocus());
  QTest::keyClicks(filter, QStringLiteral(".DEB"));
  QCOMPARE(left()->visibleItemCount(), 2);
  QVERIFY(left()->view()->isRowHidden(rowOf(left(), QStringLiteral("notes.txt"))));
  QVERIFY(!left()->view()->isRowHidden(rowOf(left(), QStringLiteral(".."))));
  // Select All only takes what is shown.
  left()->selectAllRows();
  QCOMPARE(left()->selectedLocalPaths().size(), 2);
  // Wildcards work too.
  filter->setText(QStringLiteral("a*.deb"));
  QCOMPARE(left()->visibleItemCount(), 1);
  // Escape clears the filter and returns to the list.
  QTest::keyClick(filter, Qt::Key_Escape);
  QCOMPARE(filter->text(), QString());
  QCOMPARE(left()->visibleItemCount(), 3);
  // Changing folder clears the filter.
  filter->setText(QStringLiteral("notes"));
  QDir().mkpath(work(QStringLiteral("sub")));
  openFolder(left(), work(QStringLiteral("sub")));
  QCOMPARE(filter->text(), QString());
  QCOMPARE(left()->nameFilter(), QString());
  QVERIFY(window()->windowTitle().startsWith(QStringLiteral("sub")));
}

void MainWindowActionsTest::cliCompressQuick()
{
  m_window.reset(); // the command line runs without a main window
  QVERIFY(writeFile(work(QStringLiteral("Pictures/a.jpg")), "a"));
  QVERIFY(writeFile(work(QStringLiteral("Pictures/b.jpg")), "b"));
  PromptDispatcher prompts;
  const QStringList inputs = {work(QStringLiteral("Pictures/a.jpg")), work(QStringLiteral("Pictures/b.jpg"))};
  QCOMPARE(CommandLine::compressQuick(inputs, QStringLiteral("zip"), false, &prompts), 0);
  QCOMPARE(archiveNames(work(QStringLiteral("Pictures/Pictures.zip"))),
           (QStringList{QStringLiteral("a.jpg"), QStringLiteral("b.jpg")}));
  QCOMPARE(CommandLine::compressQuick({inputs.first()}, QStringLiteral("7z"), false, &prompts), 0);
  QCOMPARE(archiveNames(work(QStringLiteral("Pictures/a.7z"))), QStringList{QStringLiteral("a.jpg")});

  // Existing archive: asks, and "No" leaves it untouched.
  m_script->expect(QStringLiteral("update?"), [](QWidget *d) { buttonNamed(d, QStringLiteral("No"))->click(); });
  QCOMPARE(CommandLine::compressQuick({inputs.first()}, QStringLiteral("7z"), false, &prompts), 130);
  finishScript();
  // --overwrite replaces it.
  QVERIFY(writeFile(work(QStringLiteral("Pictures/a.jpg")), "a2"));
  QCOMPARE(CommandLine::compressQuick({inputs.first()}, QStringLiteral("7z"), true, &prompts), 0);
  QCOMPARE(archiveNames(work(QStringLiteral("Pictures/a.7z"))), QStringList{QStringLiteral("a.jpg")});

  // Missing input reports an error and does not create anything.
  m_script->expect(QStringLiteral("missing"), [](QWidget *d) { clickAccept(d); });
  QCOMPARE(CommandLine::compressQuick({work(QStringLiteral("nope.txt"))}, QStringLiteral("7z"), false, &prompts), 2);
  finishScript();
  QVERIFY(!QFileInfo::exists(work(QStringLiteral("nope.7z"))));
}

void MainWindowActionsTest::cliExtractModes()
{
  m_window.reset();
  QVERIFY(writeFile(work(QStringLiteral("in/x.txt")), "x"));
  const QString archive = makeArchive(work(QStringLiteral("bundle.tar.7z")), {work(QStringLiteral("in/x.txt"))});
  QVERIFY(!archive.isEmpty());
  PromptDispatcher prompts;
  QCOMPARE(CommandLine::extract({archive}, CommandLine::ExtractMode::ToFolder, QString(), &prompts), 0);
  QCOMPARE(readFile(work(QStringLiteral("bundle/x.txt"))), QByteArray("x"));
  QCOMPARE(CommandLine::extract({archive}, CommandLine::ExtractMode::Here, QString(), &prompts), 0);
  QCOMPARE(readFile(work(QStringLiteral("x.txt"))), QByteArray("x"));
  QCOMPARE(CommandLine::extract({archive}, CommandLine::ExtractMode::Directory, work(QStringLiteral("explicit")), &prompts), 0);
  QCOMPARE(readFile(work(QStringLiteral("explicit/x.txt"))), QByteArray("x"));

  m_script->expect(QStringLiteral("dialog"), [this](QWidget *d) {
    QVERIFY(qobject_cast<ExtractDialog *>(d));
    QCOMPARE(d->findChildren<QLineEdit *>().first()->text(), work(QStringLiteral("bundle")));
    d->findChildren<QLineEdit *>().first()->setText(work(QStringLiteral("asked")));
    clickAccept(d);
  });
  QCOMPARE(CommandLine::extract({archive}, CommandLine::ExtractMode::Ask, QString(), &prompts), 0);
  finishScript();
  QCOMPARE(readFile(work(QStringLiteral("asked/x.txt"))), QByteArray("x"));
}

void MainWindowActionsTest::cliTestReportsResult()
{
  m_window.reset();
  QVERIFY(writeFile(work(QStringLiteral("in/x.txt")), "x"));
  const QString good = makeArchive(work(QStringLiteral("good.zip")), {work(QStringLiteral("in/x.txt"))});
  QVERIFY(writeFile(work(QStringLiteral("bad.zip")), "garbage"));
  PromptDispatcher prompts;
  m_script->expect(QStringLiteral("ok"), [](QWidget *d) { clickAccept(d); });
  QCOMPARE(CommandLine::test({good}, &prompts), 0);
  finishScript();
  QVERIFY(m_script->seen.last().contains(QStringLiteral("good.zip")));
  m_script->reset();
  m_script->expect(QStringLiteral("failed"), [](QWidget *d) { clickAccept(d); });
  QCOMPARE(CommandLine::test({work(QStringLiteral("bad.zip"))}, &prompts), 1);
  finishScript();
  QVERIFY(m_script->seen.last().contains(QStringLiteral("bad.zip")));
}

void MainWindowActionsTest::cliAddDialog()
{
  m_window.reset();
  QVERIFY(writeFile(work(QStringLiteral("proj/a.c")), "int main;"));
  PromptDispatcher prompts;
  m_script->expect(QStringLiteral("add"), [this](QWidget *d) {
    QVERIFY(qobject_cast<CompressDialog *>(d));
    QCOMPARE(d->findChildren<QLineEdit *>().first()->text(), work(QStringLiteral("proj/a.7z")));
    clickAccept(d);
  });
  QCOMPARE(CommandLine::compressAsk({work(QStringLiteral("proj/a.c"))}, &prompts), 0);
  finishScript();
  QCOMPARE(archiveNames(work(QStringLiteral("proj/a.7z"))), QStringList{QStringLiteral("a.c")});
}

void MainWindowActionsTest::desktopScriptsInstallAndRun()
{
  m_window.reset();
  const QString dir = DesktopIntegration::scriptsDirectory();
  QVERIFY2(dir.contains(QStringLiteral(".qttest")), qPrintable(dir)); // never the real home in tests
  QDir(dir).removeRecursively();
  QDir().mkpath(dir);
  QVERIFY(writeFile(dir + QStringLiteral("/My own script"), "#!/bin/sh\necho mine\n"));

  QString error;
  QVERIFY2(DesktopIntegration::syncFileManagerScripts(true, &error, QStringLiteral(SEVENZIPLX_BINARY)),
           qPrintable(error));
  for (const DesktopIntegration::Script &script : DesktopIntegration::fileManagerScripts())
  {
    const QFileInfo info(dir + QLatin1Char('/') + script.fileName);
    QVERIFY2(info.isFile() && info.isExecutable(), qPrintable(script.fileName));
  }

  // Run "Compress to zip" the way the desktop does: paths in the environment.
  QVERIFY(writeFile(work(QStringLiteral("Desk/one file.txt")), "1"));
  QVERIFY(writeFile(work(QStringLiteral("Desk/two*.txt")), "2"));
  QProcess process;
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
  env.insert(QStringLiteral("NAUTILUS_SCRIPT_SELECTED_FILE_PATHS"),
             work(QStringLiteral("Desk/one file.txt")) + QLatin1Char('\n') +
                 work(QStringLiteral("Desk/two*.txt")) + QLatin1Char('\n'));
  process.setProcessEnvironment(env);
  process.start(dir + QStringLiteral("/Compress to zip"), {});
  QVERIFY(process.waitForFinished(30000));
  QCOMPARE(process.exitCode(), 0);
  QCOMPARE(archiveNames(work(QStringLiteral("Desk/Desk.zip"))),
           (QStringList{QStringLiteral("one file.txt"), QStringLiteral("two*.txt")}));

  // And "Extract to Folder" with the paths as arguments instead.
  QProcess extract;
  env.remove(QStringLiteral("NAUTILUS_SCRIPT_SELECTED_FILE_PATHS"));
  extract.setProcessEnvironment(env);
  extract.start(dir + QStringLiteral("/Extract to Folder"), {work(QStringLiteral("Desk/Desk.zip"))});
  QVERIFY(extract.waitForFinished(30000));
  QCOMPARE(extract.exitCode(), 0);
  QCOMPARE(readFile(work(QStringLiteral("Desk/Desk/one file.txt"))), QByteArray("1"));

  // Disabling removes only our scripts.
  QVERIFY(DesktopIntegration::syncFileManagerScripts(false));
  QVERIFY(!QFileInfo::exists(dir + QStringLiteral("/Extract Here")));
  QVERIFY(QFileInfo::exists(dir + QStringLiteral("/My own script")));
  QDir(dir).removeRecursively();
}

QTEST_MAIN(MainWindowActionsTest)
#include "test_mainwindow_actions.moc"
