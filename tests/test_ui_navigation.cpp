// Widget-level tests for archive navigation, quick compression naming, dialog
// form alignment and progress dialog timing.  Runs on the offscreen platform.

#include <QtTest/QtTest>

#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableView>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include "app/OperationRunner.h"
#include "backend/CompressOperation.h"
#include "backend/PromptDispatcher.h"
#include "backend/SevenZipEngine.h"
#include "models/ArchiveFolderModel.h"
#include "models/LocalFsModel.h"
#include "ui/Theme.h"
#include "ui/dialogs/CompressDialog.h"
#include "ui/dialogs/DialogLayout.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/widgets/FilePanel.h"

namespace {

class SilentPrompts final : public PromptDispatcher
{
public:
  bool askPassword(QString *, bool *ok, const QString &) override
  {
    if (ok) *ok = false;
    return false;
  }
  OverwriteAnswer askOverwrite(const OverwriteInfo &) override { return OverwriteAnswer::Yes; }
  bool confirm(const QString &, const QString &, bool) override { return true; }
  void showError(const QString &, const QString &text) override { errors << text; }
  void showInfo(const QString &, const QString &) override {}
  QStringList errors;
};

bool writeFile(const QString &path, const QByteArray &contents)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(contents) == contents.size();
}

QString makeArchive(const QString &archivePath, const QStringList &inputs, PromptDispatcher *prompts)
{
  CompressOptions options;
  options.archivePath = archivePath;
  options.formatIndex = SevenZipEngine::instance().formatIndex(
      QFileInfo(archivePath).suffix() == QLatin1String("zip") ? QStringLiteral("zip")
                                                              : QStringLiteral("7z"));
  options.level = 1;
  const OperationResult result = compressFiles(inputs, options, nullptr, prompts);
  return result.ok ? archivePath : QString();
}

int itemNamed(const ArchiveFolderModel *model, const QString &name)
{
  for (int item = 0; item < model->itemCount(); ++item)
  {
    QString entry = model->entry(item).name;
    while (entry.endsWith(QLatin1Char('/')))
      entry.chop(1);
    if (entry == name)
      return item;
  }
  return -1;
}

void waitForLocal(FilePanel *panel)
{
  QTRY_VERIFY_WITH_TIMEOUT(!panel->localModel()->isLoading(), 5000);
  QTest::qWait(20); // selection restores are queued after loading finishes
}

QStringList selectedNames(const FilePanel *panel)
{
  QStringList names;
  if (panel->mode() == FilePanel::ArchiveMode)
  {
    for (int item : panel->selectedArchiveRows())
      names << panel->archiveModel()->entry(item).name;
  }
  else
  {
    for (const QString &path : panel->selectedLocalPaths())
      names << QFileInfo(path).fileName();
  }
  return names;
}

} // namespace

class UiNavigationTest : public QObject
{
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void archiveModelParentRow();
  void parentRowCanBeHidden();
  void activatingParentRowGoesUp();
  void goUpFromArchiveRootReturnsToFolder();
  void selectAllSkipsParentRow();
  void nestedArchiveGoUp();
  void localGoUpSelectsPreviousFolder();
  void rightClickKeepsMultiSelection();
  void formLabelsShareFieldGeometry();
  void compressDialogDefaultName();
  void quickOperationsDoNotFlashProgress();
  void slowOperationsShowProgress();

private:
  QString freshDir();
  QString demoArchive();

  QTemporaryDir m_root;
  SilentPrompts m_prompts;
  int m_counter = 0;
};

void UiNavigationTest::initTestCase()
{
  QVERIFY(m_root.isValid());
  QString error;
  QVERIFY2(SevenZipEngine::instance().initialize(&error), qPrintable(error));
  Theme::apply(*qApp, Theme::Mode::Dark);
}

void UiNavigationTest::cleanupTestCase()
{
  SevenZipEngine::instance().shutdown();
}

QString UiNavigationTest::freshDir()
{
  const QString dir = m_root.path() + QStringLiteral("/case%1").arg(++m_counter);
  QDir().mkpath(dir);
  return dir;
}

// <dir>/demo.7z containing sub/inner.txt and top.txt.
QString UiNavigationTest::demoArchive()
{
  const QString dir = freshDir();
  const QString source = dir + QStringLiteral("/src");
  if (!writeFile(source + QStringLiteral("/sub/inner.txt"), "inner") ||
      !writeFile(source + QStringLiteral("/top.txt"), "top"))
    return {};
  return makeArchive(dir + QStringLiteral("/demo.7z"),
                     {source + QStringLiteral("/sub"), source + QStringLiteral("/top.txt")},
                     &m_prompts);
}

void UiNavigationTest::archiveModelParentRow()
{
  const QString archive = demoArchive();
  QVERIFY(!archive.isEmpty());
  ArchiveFolderModel model;
  QCOMPARE(model.rowCount(), 0);
  QVERIFY(!model.hasParentRow());

  QString error;
  QVERIFY2(model.open(archive, QString(), nullptr, &m_prompts, &error), qPrintable(error));
  QCOMPARE(model.itemCount(), 2);
  QCOMPARE(model.rowCount(), 3);
  QVERIFY(model.isParentRow(0));
  QVERIFY(!model.isParentRow(1));
  QCOMPARE(model.data(model.index(0, ArchiveFolderModel::ColumnName)).toString(), QStringLiteral(".."));
  QVERIFY(!(model.flags(model.index(0, 0)) & Qt::ItemIsDragEnabled));
  QVERIFY(model.flags(model.index(1, 0)) & Qt::ItemIsDragEnabled);

  QCOMPARE(model.itemForRow(0), -1);
  QCOMPARE(model.itemForRow(1), 0);
  QCOMPARE(model.itemForRow(3), -1);
  QCOMPARE(model.rowForItem(0), 1);
  QCOMPARE(model.rowForItem(1), 2);
  QCOMPARE(model.rowForItem(2), -1);

  // Every view row after ".." shows the entry it maps to.
  for (int row = 1; row < model.rowCount(); ++row)
  {
    QString name = model.entry(model.itemForRow(row)).name;
    QCOMPARE(model.data(model.index(row, ArchiveFolderModel::ColumnName)).toString(), name);
  }

  const int sub = itemNamed(&model, QStringLiteral("sub"));
  QVERIFY(sub >= 0);
  QVERIFY(model.enter(sub));
  QCOMPARE(model.itemCount(), 1);
  QCOMPARE(model.rowCount(), 2);
  QVERIFY(model.isParentRow(0));
  QVERIFY(model.up());
  QCOMPARE(model.itemCount(), 2);

  model.close();
  QCOMPARE(model.rowCount(), 0);
}

void UiNavigationTest::parentRowCanBeHidden()
{
  const QString archive = demoArchive();
  FilePanel panel;
  panel.setShowDots(false);
  QString error;
  QVERIFY2(panel.openArchive(archive, nullptr, &m_prompts, &error), qPrintable(error));
  ArchiveFolderModel *model = panel.archiveModel();
  QCOMPARE(model->rowCount(), model->itemCount());
  QVERIFY(!panel.isParentRow(0));
  QCOMPARE(model->itemForRow(0), 0);

  panel.setShowDots(true);
  QCOMPARE(model->rowCount(), model->itemCount() + 1);
  QVERIFY(panel.isParentRow(0));
}

void UiNavigationTest::activatingParentRowGoesUp()
{
  const QString archive = demoArchive();
  FilePanel panel;
  QString error;
  QVERIFY2(panel.openArchive(archive, nullptr, &m_prompts, &error), qPrintable(error));
  ArchiveFolderModel *model = panel.archiveModel();

  const int sub = itemNamed(model, QStringLiteral("sub"));
  QVERIFY(sub >= 0);
  emit panel.view()->activated(model->index(model->rowForItem(sub), 0));
  QCOMPARE(panel.archiveRelativePath(), QStringLiteral("sub"));
  QVERIFY(panel.displayPath().endsWith(QStringLiteral("demo.7z/sub")));
  QVERIFY(panel.isParentRow(0));

  // Double-clicking ".." returns to the root with the folder we left selected.
  emit panel.view()->activated(model->index(0, 0));
  QCOMPARE(panel.archiveRelativePath(), QString());
  QCOMPARE(selectedNames(&panel), QStringList{QStringLiteral("sub")});
  QCOMPARE(panel.currentArchiveItem(), itemNamed(model, QStringLiteral("sub")));
}

void UiNavigationTest::goUpFromArchiveRootReturnsToFolder()
{
  const QString archive = demoArchive();
  const QString folder = QFileInfo(archive).absolutePath();
  FilePanel panel;
  panel.setLocalPath(QDir::homePath()); // opened from somewhere else
  waitForLocal(&panel);
  QString error;
  QVERIFY2(panel.openArchive(archive, nullptr, &m_prompts, &error), qPrintable(error));
  QVERIFY(panel.isArchive());

  // Up at the archive root closes it and shows the folder that contains it.
  QVERIFY(panel.goUp());
  QCOMPARE(panel.mode(), FilePanel::LocalMode);
  QVERIFY(!panel.isArchive());
  QCOMPARE(panel.localPath(), folder);
  waitForLocal(&panel);
  QCOMPARE(panel.selectedLocalPaths(), QStringList{archive});

  // The same through the ".." row.
  QVERIFY2(panel.openArchive(archive, nullptr, &m_prompts, &error), qPrintable(error));
  emit panel.view()->activated(panel.archiveModel()->index(0, 0));
  QCOMPARE(panel.mode(), FilePanel::LocalMode);
  QCOMPARE(panel.localPath(), folder);
  waitForLocal(&panel);
  QCOMPARE(panel.selectedLocalPaths(), QStringList{archive});
}

void UiNavigationTest::selectAllSkipsParentRow()
{
  const QString archive = demoArchive();
  FilePanel panel;
  QString error;
  QVERIFY2(panel.openArchive(archive, nullptr, &m_prompts, &error), qPrintable(error));
  panel.selectAllRows();
  QItemSelectionModel *selection = panel.view()->selectionModel();
  QVERIFY(!selection->isRowSelected(0, QModelIndex()));
  QCOMPARE(panel.selectedArchiveRows(), (QVector<int>{0, 1}));

  // Selecting ".." alone never yields an archive item.
  selection->clearSelection();
  selection->select(panel.archiveModel()->index(0, 0),
                    QItemSelectionModel::Select | QItemSelectionModel::Rows);
  panel.view()->setCurrentIndex(panel.archiveModel()->index(0, 0));
  QVERIFY(panel.selectedArchiveRows().isEmpty());
  QCOMPARE(panel.currentArchiveItem(), -1);

  panel.invertSelection();
  QVERIFY(!selection->isRowSelected(0, QModelIndex()));
  QCOMPARE(panel.selectedArchiveRows().size(), 2);
}

void UiNavigationTest::nestedArchiveGoUp()
{
  const QString dir = freshDir();
  QVERIFY(writeFile(dir + QStringLiteral("/payload/deep.txt"), "deep"));
  const QString inner = makeArchive(dir + QStringLiteral("/inner.zip"),
                                    {dir + QStringLiteral("/payload/deep.txt")}, &m_prompts);
  QVERIFY(!inner.isEmpty());
  QVERIFY(writeFile(dir + QStringLiteral("/other.txt"), "other"));
  const QString outer = makeArchive(dir + QStringLiteral("/outer.7z"),
                                    {inner, dir + QStringLiteral("/other.txt")}, &m_prompts);
  QVERIFY(!outer.isEmpty());

  FilePanel panel;
  QString error;
  QVERIFY2(panel.openArchive(outer, nullptr, &m_prompts, &error), qPrintable(error));
  const int innerItem = itemNamed(panel.archiveModel(), QStringLiteral("inner.zip"));
  QVERIFY(innerItem >= 0);
  QVERIFY2(panel.openNestedArchive(innerItem, nullptr, &m_prompts, &error), qPrintable(error));
  QVERIFY(panel.isNestedArchive());
  QVERIFY(panel.displayPath().endsWith(QStringLiteral("outer.7z/inner.zip")));
  QVERIFY(itemNamed(panel.archiveModel(), QStringLiteral("deep.txt")) >= 0);
  QVERIFY(panel.isParentRow(0));

  QVERIFY(panel.goUp());
  QVERIFY(!panel.isNestedArchive());
  QVERIFY(panel.displayPath().endsWith(QStringLiteral("outer.7z")));
  QCOMPARE(selectedNames(&panel), QStringList{QStringLiteral("inner.zip")});

  QVERIFY(panel.goUp());
  QCOMPARE(panel.mode(), FilePanel::LocalMode);
  QCOMPARE(panel.localPath(), dir);
  waitForLocal(&panel);
  QCOMPARE(panel.selectedLocalPaths(), QStringList{outer});
}

void UiNavigationTest::localGoUpSelectsPreviousFolder()
{
  const QString dir = freshDir();
  QVERIFY(writeFile(dir + QStringLiteral("/alpha/file.txt"), "a"));
  QVERIFY(writeFile(dir + QStringLiteral("/beta/file.txt"), "b"));
  FilePanel panel;
  panel.setLocalPath(dir + QStringLiteral("/beta"));
  waitForLocal(&panel);
  QVERIFY(panel.goUp());
  QCOMPARE(panel.localPath(), dir);
  waitForLocal(&panel);
  QCOMPARE(panel.selectedLocalPaths(), QStringList{dir + QStringLiteral("/beta")});
}

void UiNavigationTest::rightClickKeepsMultiSelection()
{
  const QString dir = freshDir();
  for (const QString &name : {QStringLiteral("a.deb"), QStringLiteral("b.deb"), QStringLiteral("c.deb")})
    QVERIFY(writeFile(dir + QLatin1Char('/') + name, "x"));
  FilePanel panel;
  panel.resize(700, 400);
  panel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&panel));
  panel.setLocalPath(dir);
  waitForLocal(&panel);

  QTableView *view = panel.view();
  const QStringList wanted = {dir + QStringLiteral("/a.deb"), dir + QStringLiteral("/b.deb")};
  panel.selectLocalPaths(wanted);
  QCOMPARE(panel.selectedLocalPaths().size(), 2);

  int rowB = -1, rowC = -1;
  for (int row = 0; row < view->model()->rowCount(); ++row)
  {
    const QString name = panel.localModel()->fileInfo(row).fileName();
    if (name == QLatin1String("b.deb")) rowB = row;
    if (name == QLatin1String("c.deb")) rowC = row;
  }
  QVERIFY(rowB >= 0 && rowC >= 0);

  int menus = 0;
  connect(&panel, &FilePanel::contextMenuRequested, this, [&menus] { ++menus; });

  // Right-clicking one of the selected rows keeps the whole selection.
  const QPoint onB = view->visualRect(view->model()->index(rowB, 1)).center();
  QTest::mousePress(view->viewport(), Qt::RightButton, {}, onB);
  QTest::mouseRelease(view->viewport(), Qt::RightButton, {}, onB);
  QContextMenuEvent menuOnB(QContextMenuEvent::Mouse, onB, view->viewport()->mapToGlobal(onB));
  QCoreApplication::sendEvent(view->viewport(), &menuOnB);
  QCOMPARE(menus, 1);
  QStringList selected = panel.selectedLocalPaths();
  selected.sort();
  QCOMPARE(selected, wanted);
  QCOMPARE(view->currentIndex().row(), rowB);

  // Right-clicking an unselected row selects just that row.
  const QPoint onC = view->visualRect(view->model()->index(rowC, 1)).center();
  QTest::mousePress(view->viewport(), Qt::RightButton, {}, onC);
  QTest::mouseRelease(view->viewport(), Qt::RightButton, {}, onC);
  QContextMenuEvent menuOnC(QContextMenuEvent::Mouse, onC, view->viewport()->mapToGlobal(onC));
  QCoreApplication::sendEvent(view->viewport(), &menuOnC);
  QCOMPARE(panel.selectedLocalPaths(), QStringList{dir + QStringLiteral("/c.deb")});
}

void UiNavigationTest::formLabelsShareFieldGeometry()
{
  QDialog dialog;
  auto *form = new QFormLayout(&dialog);
  configureDialogForm(form);
  auto *edit = new QLineEdit(&dialog);
  auto *combo = new QComboBox(&dialog);
  combo->addItem(QStringLiteral("Item"));
  auto *spin = new QSpinBox(&dialog);
  form->addRow(QStringLiteral("Name:"), edit);
  form->addRow(QStringLiteral("Kind:"), combo);
  form->addRow(QStringLiteral("Threads:"), spin);
  dialog.show(); // the theme aligns form labels when a dialog is shown
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));

  // Every input control has the same height, so labels and text line up
  // across rows.
  QCOMPARE(combo->height(), edit->height());
  QCOMPARE(spin->height(), edit->height());

  for (QWidget *field : {static_cast<QWidget *>(edit), static_cast<QWidget *>(combo),
                         static_cast<QWidget *>(spin)})
  {
    auto *label = qobject_cast<QLabel *>(form->labelForField(field));
    QVERIFY(label);
    QVERIFY(label->alignment() & Qt::AlignVCenter);
    QCOMPARE(label->indent(), 0);
    QCOMPARE(label->geometry().top(), field->geometry().top());
    QCOMPARE(label->geometry().height(), field->geometry().height());
  }
}

void UiNavigationTest::compressDialogDefaultName()
{
  const QString dir = freshDir() + QStringLiteral("/photos");
  QVERIFY(writeFile(dir + QStringLiteral("/a.jpg"), "a"));
  QVERIFY(writeFile(dir + QStringLiteral("/lib.d/x"), "x"));

  {
    CompressDialog dialog({dir + QStringLiteral("/lib.d")}, dir);
    QCOMPARE(dialog.options().archivePath, dir + QStringLiteral("/lib.d.7z"));
  }
  {
    CompressDialog dialog({dir + QStringLiteral("/a.jpg"), dir + QStringLiteral("/lib.d")}, dir);
    QCOMPARE(dialog.options().archivePath, dir + QStringLiteral("/photos.7z"));
  }
  {
    // Nothing selected: the folder itself goes into an archive beside it.
    const QuickArchiveTarget target = quickArchiveTarget({}, dir, QStringLiteral("7z"));
    CompressDialog dialog(target.inputs, QFileInfo(target.archivePath).absolutePath());
    QCOMPARE(dialog.options().archivePath, QFileInfo(dir).absolutePath() + QStringLiteral("/photos.7z"));
  }
}

void UiNavigationTest::quickOperationsDoNotFlashProgress()
{
  OperationRunner runner(nullptr, &m_prompts);
  bool sawDialog = false;
  QTimer probe;
  connect(&probe, &QTimer::timeout, this, [&sawDialog] {
    for (QWidget *w : QApplication::topLevelWidgets())
      if (qobject_cast<ProgressDialog *>(w) && w->isVisible())
        sawDialog = true;
  });
  probe.start(10);
  const OperationResult result = runner.run(QStringLiteral("Quick"), [](ProgressState *, PromptDispatcher *) {
    QThread::msleep(60);
    OperationResult r;
    r.ok = true;
    r.files = 7;
    return r;
  });
  probe.stop();
  QVERIFY(result.ok);
  QCOMPARE(result.files, quint64(7));
  QVERIFY(!sawDialog);
}

void UiNavigationTest::slowOperationsShowProgress()
{
  OperationRunner runner(nullptr, &m_prompts);
  bool sawDialog = false;
  QTimer probe;
  connect(&probe, &QTimer::timeout, this, [&sawDialog] {
    for (QWidget *w : QApplication::topLevelWidgets())
      if (qobject_cast<ProgressDialog *>(w) && w->isVisible())
        sawDialog = true;
  });
  probe.start(10);
  const OperationResult result = runner.run(QStringLiteral("Slow"), [](ProgressState *, PromptDispatcher *) {
    QThread::msleep(900);
    OperationResult r;
    r.ok = true;
    return r;
  });
  probe.stop();
  QVERIFY(result.ok);
  QVERIFY(sawDialog);
  for (QWidget *w : QApplication::topLevelWidgets())
    if (qobject_cast<ProgressDialog *>(w))
      QVERIFY(!w->isVisible());
}

QTEST_MAIN(UiNavigationTest)
#include "test_ui_navigation.moc"
