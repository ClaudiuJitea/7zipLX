#include "app/MainWindow.h"
#include "backend/ArchiveSession.h"
#include "backend/CompressOperation.h"
#include "backend/MessageDialog.h"
#include "backend/ProgressState.h"
#include "backend/PromptDispatcher.h"
#include "backend/SevenZipEngine.h"
#include "ui/Theme.h"
#include "app/OperationRunner.h"
#include "app/CommandLine.h"
#include "app/DesktopIntegration.h"

#include <QApplication>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QSettings>
#include <QLibraryInfo>
#include <QTranslator>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <cstdio>

int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("7zipLX"));
  QApplication::setApplicationDisplayName(QStringLiteral("7zipLX"));
  QApplication::setOrganizationName(QStringLiteral("7zipLX"));
  QApplication::setOrganizationDomain(QStringLiteral("7ziplx.local"));
  QApplication::setApplicationVersion(QStringLiteral(SEVENZIPLX_VERSION));
  QGuiApplication::setDesktopFileName(QStringLiteral("7ziplx"));
  Theme::apply(app, Theme::modeFromString(
      QSettings().value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString()));
  QApplication::setWindowIcon(QIcon::fromTheme(
      QStringLiteral("7ziplx"), QIcon(QStringLiteral(":/icons/7ziplx-256.png"))));

  QString language = QSettings().value(QStringLiteral("ui/language"), QStringLiteral("system")).toString();
  if (language == QLatin1String("system"))
    language = QLocale::system().name().section(QLatin1Char('_'), 0, 0);
  if (!language.isEmpty() && language != QLatin1String("en"))
  {
    auto *translator = new QTranslator(&app);
    const QStringList dirs = {
        QApplication::applicationDirPath() + QStringLiteral("/translations"),
        QApplication::applicationDirPath() + QStringLiteral("/../share/7ziplx/translations"),
        QStringLiteral("/usr/share/7ziplx/translations"),
        QLibraryInfo::path(QLibraryInfo::TranslationsPath)};
    for (const QString &dir : dirs)
    {
      if (translator->load(QStringLiteral("7ziplx_") + language, dir))
      {
        app.installTranslator(translator);
        break;
      }
    }
  }

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("Native Linux GUI for 7-Zip"));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument(QStringLiteral("path"),
                              QStringLiteral("Archive or folder to open, or the inputs of an action below."),
                              QStringLiteral("[path...]"));
  QCommandLineOption extractHere(QStringLiteral("extract-here"),
                                 QStringLiteral("Extract each archive into the folder that contains it."));
  QCommandLineOption extractToFolder(QStringLiteral("extract-to-folder"),
                                     QStringLiteral("Extract each archive into a new folder named after it."));
  QCommandLineOption extractTo(QStringLiteral("extract-to"),
                               QStringLiteral("Extract the archives into <directory>."),
                               QStringLiteral("directory"));
  QCommandLineOption extractAsk(QStringLiteral("extract"),
                                QStringLiteral("Show the Extract dialog for the archives."));
  QCommandLineOption testOption(QStringLiteral("test"), QStringLiteral("Test the archives."));
  QCommandLineOption compress(QStringLiteral("compress"),
                              QStringLiteral("Add all inputs to <name>.7z (see --format) next to them."));
  QCommandLineOption format(QStringLiteral("format"),
                            QStringLiteral("Archive format for --compress: 7z (default) or zip."),
                            QStringLiteral("format"), QStringLiteral("7z"));
  QCommandLineOption compressTo(QStringLiteral("compress-to"),
                                QStringLiteral("Add all inputs to <archive>."),
                                QStringLiteral("archive"));
  QCommandLineOption addAsk(QStringLiteral("add"),
                            QStringLiteral("Show the Add to Archive dialog for the inputs."));
  QCommandLineOption overwrite(QStringLiteral("overwrite"),
                               QStringLiteral("Replace an existing archive instead of adding to it."));
  QCommandLineOption installMenus(QStringLiteral("install-menus"),
                                  QStringLiteral("Add the 7zipLX actions to the desktop/Files Scripts menu and exit."));
  QCommandLineOption removeMenus(QStringLiteral("remove-menus"),
                                 QStringLiteral("Remove the 7zipLX actions from the Scripts menu and exit."));
  for (const QCommandLineOption &option : {extractHere, extractToFolder, extractTo, extractAsk, testOption,
                                           compress, format, compressTo, addAsk, overwrite, installMenus,
                                           removeMenus})
    parser.addOption(option);
  parser.process(app);

  if (parser.isSet(installMenus) || parser.isSet(removeMenus))
  {
    const bool enable = parser.isSet(installMenus);
    QSettings().setValue(QStringLiteral("integration/fileManagerMenus"), enable);
    QString syncError;
    if (!DesktopIntegration::syncFileManagerScripts(enable, &syncError))
    {
      fprintf(stderr, "%s\n", qPrintable(syncError));
      return 1;
    }
    printf("%s %s\n", enable ? "Installed" : "Removed", qPrintable(DesktopIntegration::scriptsDirectory()));
    return 0;
  }

  QString error;
  if (!SevenZipEngine::instance().initialize(&error))
  {
    MessageDialog::show(nullptr, QStringLiteral("7zipLX"), QStringLiteral("Cannot start 7zipLX"),
                        error, MessageDialog::Kind::Error);
    return 1;
  }

  const QStringList inputs = parser.positionalArguments();
  int job = -1;
  {
    PromptDispatcher prompts;
    if (parser.isSet(extractHere))
      job = CommandLine::extract(inputs, CommandLine::ExtractMode::Here, QString(), &prompts);
    else if (parser.isSet(extractToFolder))
      job = CommandLine::extract(inputs, CommandLine::ExtractMode::ToFolder, QString(), &prompts);
    else if (parser.isSet(extractTo))
      job = CommandLine::extract(inputs, CommandLine::ExtractMode::Directory, parser.value(extractTo), &prompts);
    else if (parser.isSet(extractAsk))
      job = CommandLine::extract(inputs, CommandLine::ExtractMode::Ask, QString(), &prompts);
    else if (parser.isSet(testOption))
      job = CommandLine::test(inputs, &prompts);
    else if (parser.isSet(compressTo))
      job = CommandLine::compressTo(inputs, parser.value(compressTo), parser.isSet(overwrite), &prompts);
    else if (parser.isSet(compress))
      job = CommandLine::compressQuick(inputs, parser.value(format), parser.isSet(overwrite), &prompts);
    else if (parser.isSet(addAsk))
      job = CommandLine::compressAsk(inputs, &prompts);
  }
  if (job >= 0)
  {
    SevenZipEngine::instance().shutdown();
    return job;
  }

  DesktopIntegration::syncFileManagerScripts(
      QSettings().value(QStringLiteral("integration/fileManagerMenus"), true).toBool());

  int ret = 0;
  {
    MainWindow window;
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty())
      window.setTwoPanels(false);
    window.show();
    if (!args.isEmpty())
    {
      const QString path = args.first();
      QMetaObject::invokeMethod(&window, [&window, path] { window.openPath(path); }, Qt::QueuedConnection);
    }
    ret = app.exec();
  }
  SevenZipEngine::instance().shutdown();
  return ret;
}
