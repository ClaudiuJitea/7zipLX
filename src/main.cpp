#include "app/MainWindow.h"
#include "backend/MessageDialog.h"
#include "backend/SevenZipEngine.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QSettings>
#include <QLibraryInfo>
#include <QTranslator>
#include <QDir>
#include <QIcon>

int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("7zipLX"));
  QApplication::setApplicationDisplayName(QStringLiteral("7zipLX"));
  QApplication::setOrganizationName(QStringLiteral("7zipLX"));
  QApplication::setOrganizationDomain(QStringLiteral("7ziplx.local"));
  QApplication::setApplicationVersion(QStringLiteral("0.3.0"));
  Theme::apply(app);
  QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/7ziplx-256.png")));

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
  parser.addPositionalArgument(QStringLiteral("archive"), QStringLiteral("Archive to open."), QStringLiteral("[archive]"));
  parser.process(app);

  QString error;
  if (!SevenZipEngine::instance().initialize(&error))
  {
    MessageDialog::show(nullptr, QStringLiteral("7zipLX"), QStringLiteral("Cannot start 7zipLX"),
                        error, MessageDialog::Kind::Error);
    return 1;
  }

  int ret = 0;
  {
    MainWindow window;
    window.show();
    const QStringList args = parser.positionalArguments();
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
