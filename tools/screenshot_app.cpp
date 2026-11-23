// README screenshot generator (not installed, not built by default).
//
// Usage, from the repository root:
//   cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
//         -DBUILD_SCREENSHOTS=ON
//   cmake --build build-release -j"$(nproc)"
//   QT_QPA_PLATFORM=offscreen ./build-release/7ziplx-screenshots docs/screenshots
//
// The tool creates a demo archive and a Downloads-style local folder in a
// temporary directory, captures the real widgets with QWidget::grab(), and
// leaves the actual README composition to tools/compose_screenshots.py.

#include "app/MainWindow.h"
#include "backend/CompressOperation.h"
#include "backend/MessageDialog.h"
#include "backend/ProgressState.h"
#include "backend/PromptDispatcher.h"
#include "backend/SevenZipEngine.h"
#include "models/ArchiveFolderModel.h"
#include "models/LocalFsModel.h"
#include "ui/Theme.h"
#include "ui/dialogs/CompressDialog.h"
#include "ui/dialogs/ExtractDialog.h"
#include "ui/dialogs/HashResultDialog.h"
#include "ui/dialogs/OptionsDialog.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/widgets/FilePanel.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QTabWidget>
#include <QTableView>
#include <QThread>
#include <QTimer>

#ifndef SCREENSHOT_SOURCE_DIR
#define SCREENSHOT_SOURCE_DIR "."
#endif

namespace {

void waitForUi(int milliseconds = 400)
{
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < milliseconds)
  {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(8);
  }
}

void saveWidget(QWidget *widget, const QString &path)
{
  if (!widget)
    return;
  waitForUi(250);
  widget->grab().save(path);
  qInfo("saved %s", qPrintable(path));
}

void writeFile(const QString &path, const QByteArray &contents)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    file.write(contents);
}

void copyFile(const QString &source, const QString &target)
{
  QDir().mkpath(QFileInfo(target).absolutePath());
  QFile::copy(source, target);
}

void selectRow(FilePanel *panel, int row)
{
  if (!panel || !panel->view() || !panel->view()->model())
    return;
  QAbstractItemModel *model = panel->view()->model();
  if (row < 0 || row >= model->rowCount())
    return;
  const QModelIndex index = model->index(row, 0);
  panel->view()->selectionModel()->select(
      index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  panel->view()->setCurrentIndex(index);
  panel->view()->scrollTo(index);
}

void buildDemoTree(const QString &root)
{
  const QString source = QStringLiteral(SCREENSHOT_SOURCE_DIR);
  QDir().mkpath(root);

  const QStringList files = {
      QStringLiteral("README.md"),
      QStringLiteral("CMakeLists.txt"),
      QStringLiteral("CMakePresets.json"),
      QStringLiteral("NOTICE.md"),
      QStringLiteral("docs/USER-GUIDE.md"),
      QStringLiteral("docs/BUILD.md"),
      QStringLiteral("src/main.cpp"),
      QStringLiteral("src/app/MainWindow.cpp"),
      QStringLiteral("src/backend/ArchiveSession.cpp"),
      QStringLiteral("src/ui/Theme.cpp"),
      QStringLiteral("packaging/7ziplx.desktop"),
      QStringLiteral("scripts/build-deb.sh"),
  };
  for (const QString &relative : files)
    copyFile(source + QLatin1Char('/') + relative, root + QLatin1Char('/') + relative);

  writeFile(root + QStringLiteral("/CHANGELOG.md"),
            "# 7zipLX 0.5.1\n\n"
            "- single-pane first, two panels on F9\n"
            "- external-edit write-back for archive items\n"
            "- streamed archive hashing\n"
            "- background local folder scanning\n"
            "- system / light / dark themes\n");
  writeFile(root + QStringLiteral("/LICENSE"),
            "7-Zip is primarily LGPL-2.1-or-later. See LICENSES/ for details.\n");

  // A realistic Downloads folder for the second pane.
  const QString downloads = root + QStringLiteral("/Downloads");
  QDir().mkpath(downloads);
  writeFile(downloads + QStringLiteral("/release-notes.md"),
            "# 7zipLX 0.5.1\n\n"
            "Single-pane first. F9 adds a second pane.\n"
            "F4 edits are written back to the archive.\n");
  writeFile(downloads + QStringLiteral("/SHA256SUMS"),
            "6a076f2d6c4a9ddf6e0b2a4c8e6f5d3c  "
            "7zipLX-0.5.1-x86_64.AppImage\n"
            "a3f3d2c2b1e0f9a8c7b6a5d4e3f2a1b0  "
            "7ziplx_0.5.1-1_amd64.deb\n");
  writeFile(downloads + QStringLiteral("/7ziplx_0.5.1-1_amd64.deb"), "demo package\n");
  writeFile(downloads + QStringLiteral("/7zipLX-0.5.1-x86_64.AppImage"), "demo appimage\n");
  QDir().mkpath(downloads + QStringLiteral("/screenshots"));
  writeFile(downloads + QStringLiteral("/screenshots/hero.png"), "demo screenshot\n");
}

} // namespace

int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("7zipLX"));
  QApplication::setApplicationDisplayName(QStringLiteral("7zipLX"));
  QApplication::setOrganizationName(QStringLiteral("7zipLX"));
  QApplication::setApplicationVersion(QStringLiteral("0.5.1"));

  const QString outputDir = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                     : QStringLiteral("docs/screenshots");
  QDir().mkpath(outputDir);

  Theme::apply(app, Theme::Mode::Dark);

  const QString work = QStringLiteral("/tmp/7ziplx-demo");
  QDir(work).removeRecursively();
  QDir().mkpath(work);
  const QString demoRoot = work + QStringLiteral("/7ziplx-0.5.1");
  buildDemoTree(demoRoot);

  QString engineError;
  if (!SevenZipEngine::instance().initialize(&engineError))
  {
    qCritical("%s", qPrintable(engineError));
    return 1;
  }

  PromptDispatcher prompts;
  const QString archivePath = work + QStringLiteral("/7ziplx-0.5.1.7z");
  CompressOptions options;
  options.archivePath = archivePath;
  options.formatIndex = SevenZipEngine::instance().formatIndex(QStringLiteral("7z"));
  options.level = 7;
  const QStringList inputs = {
      demoRoot + QStringLiteral("/README.md"),
      demoRoot + QStringLiteral("/CHANGELOG.md"),
      demoRoot + QStringLiteral("/CMakeLists.txt"),
      demoRoot + QStringLiteral("/CMakePresets.json"),
      demoRoot + QStringLiteral("/NOTICE.md"),
      demoRoot + QStringLiteral("/LICENSE"),
      demoRoot + QStringLiteral("/src"),
      demoRoot + QStringLiteral("/docs"),
      demoRoot + QStringLiteral("/packaging"),
      demoRoot + QStringLiteral("/scripts"),
  };
  const OperationResult created = compressFiles(inputs, options, nullptr, &prompts);
  if (!created.ok)
  {
    qCritical("demo archive: %s", qPrintable(created.error));
    return 1;
  }
  copyFile(archivePath, demoRoot + QStringLiteral("/Downloads/7ziplx-0.5.1.7z"));

  MainWindow window;
  window.resize(1180, 760);
  window.setTwoPanels(false);
  window.show();
  waitForUi(1200);
  window.openPath(archivePath);
  waitForUi(900);

  QList<FilePanel *> panels = window.findChildren<FilePanel *>();
  FilePanel *left = nullptr;
  FilePanel *right = nullptr;
  for (FilePanel *panel : panels)
  {
    if (panel->panelId() == 0)
      left = panel;
    else if (panel->panelId() == 1)
      right = panel;
  }
  if (!left || !right)
    return 1;

  // Single-pane hero.  Focus the archive panel so archive actions are enabled.
  left->focusView();
  waitForUi(300);
  for (int row = 0; row < left->archiveModel()->rowCount(); ++row)
  {
    if (left->archiveModel()->entry(row).name == QLatin1String("README.md"))
    {
      selectRow(left, row);
      break;
    }
  }
  saveWidget(&window, outputDir + QStringLiteral("/hero.png"));

  // Two-pane view with a Downloads-style destination.
  window.setTwoPanels(true);
  right->setLocalPath(demoRoot + QStringLiteral("/Downloads"));
  waitForUi(1200);
  for (int row = 0; row < left->archiveModel()->rowCount(); ++row)
  {
    if (left->archiveModel()->entry(row).name == QLatin1String("src"))
    {
      selectRow(left, row);
      break;
    }
  }
  selectRow(right, 4);
  saveWidget(&window, outputDir + QStringLiteral("/two-panes-dark.png"));

  // Light theme variation.
  Theme::apply(app, Theme::Mode::Light);
  waitForUi(500);
  saveWidget(&window, outputDir + QStringLiteral("/two-panes-light.png"));

  // Archive context menu.
  QTimer::singleShot(350, [&outputDir] {
    if (QWidget *popup = QApplication::activePopupWidget())
    {
      popup->grab().save(outputDir + QStringLiteral("/archive-menu.png"));
      popup->close();
    }
  });
  const QModelIndex menuIndex = left->view()->model()->index(2, 0);
  const QPoint local = left->view()->visualRect(menuIndex).center();
  QContextMenuEvent menuEvent(QContextMenuEvent::Mouse, local,
                              left->view()->viewport()->mapToGlobal(local));
  QCoreApplication::sendEvent(left->view()->viewport(), &menuEvent);
  waitForUi(200);

  // Back to dark for the remaining dialogs.
  Theme::apply(app, Theme::Mode::Dark);
  waitForUi(300);

  {
    CompressDialog dialog({demoRoot + QStringLiteral("/README.md"),
                           demoRoot + QStringLiteral("/src")},
                          demoRoot);
    const QList<QLineEdit *> edits = dialog.findChildren<QLineEdit *>();
    for (QLineEdit *edit : edits)
    {
      if (edit->text().endsWith(QStringLiteral(".7z")))
      {
        edit->setText(work + QStringLiteral("/7ziplx-0.5.1.7z"));
        break;
      }
    }
    dialog.resize(600, 650);
    dialog.show();
    saveWidget(&dialog, outputDir + QStringLiteral("/compress-dialog.png"));
  }

  {
    ExtractDialog dialog;
    dialog.setArchivePath(archivePath);
    dialog.setDefaultOutputDir(work + QStringLiteral("/extracted"));
    dialog.resize(560, 440);
    dialog.show();
    saveWidget(&dialog, outputDir + QStringLiteral("/extract-dialog.png"));
  }

  {
    OptionsDialog dialog(true, true, false, false, QStringLiteral("xdg-open"),
                         QStringLiteral("xdg-open"), QStringLiteral("diff -u"),
                         QStringLiteral("system"), QStringLiteral("dark"));
    dialog.resize(640, 590);
    if (QTabWidget *tabs = dialog.findChild<QTabWidget *>())
      tabs->setCurrentIndex(0);
    dialog.show();
    saveWidget(&dialog, outputDir + QStringLiteral("/options-dialog.png"));
  }

  {
    HashResultDialog dialog(QStringLiteral("SHA-256 hash"),
                            {QStringLiteral("SHA256: 6a076f2d6c4a9ddf6e0b2a4c8e6f5d3c"
                                            "8f1b7e2a4c6d8e0f1a3b5c7d9e2f4a6b"),
                             QStringLiteral("SHA256(names): 3e5d7f9a1b2c4d6e8f0a2b4c6d8e0f"
                                            "1a3b5c7d9e2f4a6b8c0d2e4f6a8b0c2d"),
                             QStringLiteral("Files: 12   Size: 4.8 MiB")},
                            nullptr);
    dialog.resize(700, 300);
    dialog.show();
    saveWidget(&dialog, outputDir + QStringLiteral("/hash-result.png"));
  }

  {
    MessageDialog dialog;
    dialog.setWindowTitle(QStringLiteral("Update archive?"));
    dialog.setKind(MessageDialog::Kind::Question);
    dialog.setHeading(QStringLiteral("Update archive?"));
    dialog.setText(QStringLiteral("<b>Theme.cpp</b> was modified in an external editor.<br><br>"
                                 "Update it in <b>7ziplx-0.5.1.7z</b>?"),
                   true);
    auto *update = dialog.addButton(QStringLiteral("Update Archive"),
                                    QDialogButtonBox::AcceptRole);
    dialog.addButton(QStringLiteral("Keep Changes"), QDialogButtonBox::ActionRole);
    dialog.addButton(QStringLiteral("Discard"), QDialogButtonBox::RejectRole);
    dialog.setDefaultButton(update);
    dialog.resize(560, 230);
    dialog.setVisible(true);
    saveWidget(&dialog, outputDir + QStringLiteral("/external-edit.png"));
  }

  {
    ProgressState state;
    state.setTitle(QStringLiteral("Updating archive"));
    state.setStatus(QStringLiteral("Writing 7ziplx-0.5.1.7z"));
    state.setCurrentFile(QStringLiteral("src/ui/Theme.cpp"));
    state.setTotals(4823449, 12);
    state.setCompleted(3111628, 8);
    state.addMessage(QStringLiteral("Updating nested entry src/ui/Theme.cpp"));
    ProgressDialog dialog(&state, QStringLiteral("Updating archive"));
    dialog.resize(650, 420);
    dialog.show();
    saveWidget(&dialog, outputDir + QStringLiteral("/progress-dialog.png"));
  }

  SevenZipEngine::instance().shutdown();
  return 0;
}
