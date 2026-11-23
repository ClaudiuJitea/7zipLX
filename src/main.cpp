#include "app/MainWindow.h"
#include "backend/ArchiveSession.h"
#include "backend/CompressOperation.h"
#include "backend/MessageDialog.h"
#include "backend/ProgressState.h"
#include "backend/PromptDispatcher.h"
#include "backend/SevenZipEngine.h"
#include "ui/Theme.h"
#include "app/OperationRunner.h"

#include <QApplication>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QSettings>
#include <QLibraryInfo>
#include <QTranslator>
#include <QDir>
#include <QFileInfo>
#include <QIcon>

int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("7zipLX"));
  QApplication::setApplicationDisplayName(QStringLiteral("7zipLX"));
  QApplication::setOrganizationName(QStringLiteral("7zipLX"));
  QApplication::setOrganizationDomain(QStringLiteral("7ziplx.local"));
  QApplication::setApplicationVersion(QStringLiteral("0.5.2"));
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
                              QStringLiteral("Archive to open, or input paths for --compress."),
                              QStringLiteral("[path...]"));
  QCommandLineOption extractHere(QStringLiteral("extract-here"),
                                 QStringLiteral("Extract the archive into its own folder and exit."));
  QCommandLineOption extractTo(QStringLiteral("extract-to"),
                               QStringLiteral("Extract the archive into <directory> and exit."),
                               QStringLiteral("directory"));
  QCommandLineOption compress(QStringLiteral("compress"),
                              QStringLiteral("Compress each positional path to <name>.7z and exit."));
  QCommandLineOption compressTo(QStringLiteral("compress-to"),
                                QStringLiteral("Compress positional paths into <archive> and exit."),
                                QStringLiteral("archive"));
  QCommandLineOption overwrite(QStringLiteral("overwrite"),
                               QStringLiteral("Overwrite existing archives without asking."));
  parser.addOption(extractHere);
  parser.addOption(extractTo);
  parser.addOption(compress);
  parser.addOption(compressTo);
  parser.addOption(overwrite);
  parser.process(app);

  QString error;
  if (!SevenZipEngine::instance().initialize(&error))
  {
    MessageDialog::show(nullptr, QStringLiteral("7zipLX"), QStringLiteral("Cannot start 7zipLX"),
                        error, MessageDialog::Kind::Error);
    return 1;
  }

  if (parser.isSet(compress) || parser.isSet(compressTo))
  {
    const QStringList sources = parser.positionalArguments();
    if (sources.isEmpty())
    {
      MessageDialog::show(nullptr, QStringLiteral("7zipLX"), QStringLiteral("Missing input"),
                          QStringLiteral("Specify at least one file or folder to compress."),
                          MessageDialog::Kind::Error);
      SevenZipEngine::instance().shutdown();
      return 2;
    }
    for (const QString &source : sources)
    {
      if (!QFileInfo::exists(source))
      {
        MessageDialog::show(nullptr, QStringLiteral("7zipLX"), QStringLiteral("Missing input"),
                            QStringLiteral("The input does not exist: %1").arg(source),
                            MessageDialog::Kind::Error);
        SevenZipEngine::instance().shutdown();
        return 2;
      }
    }

    auto formatForArchive = [](const QString &archive) {
      const QString suffix = QFileInfo(archive).suffix().toLower();
      if (suffix == QLatin1String("zip")) return QStringLiteral("zip");
      if (suffix == QLatin1String("tar")) return QStringLiteral("tar");
      if (suffix == QLatin1String("gz") || suffix == QLatin1String("tgz")) return QStringLiteral("gzip");
      if (suffix == QLatin1String("bz2") || suffix == QLatin1String("tbz2")) return QStringLiteral("bzip2");
      if (suffix == QLatin1String("xz") || suffix == QLatin1String("txz")) return QStringLiteral("xz");
      return QStringLiteral("7z");
    };

    struct Job
    {
      QStringList sources;
      QString archive;
    };
    QVector<Job> jobs;
    if (parser.isSet(compressTo))
    {
      const QString archive = QFileInfo(parser.value(compressTo)).absoluteFilePath();
      QStringList paths;
      for (const QString &source : sources)
        paths << QFileInfo(source).absoluteFilePath();
      jobs.append({paths, archive});
    }
    else
    {
      for (const QString &source : sources)
      {
        const QFileInfo info(source);
        const QString base = info.isDir() ? info.fileName() : info.completeBaseName();
        const QString archive =
            info.absolutePath() + QLatin1Char('/') + (base.isEmpty() ? QStringLiteral("archive") : base) +
            QStringLiteral(".7z");
        if (info.absoluteFilePath() == QFileInfo(archive).absoluteFilePath())
        {
          MessageDialog::show(nullptr, QStringLiteral("7zipLX"), QStringLiteral("Invalid output"),
                              QStringLiteral("The output archive would overwrite its input: %1")
                                  .arg(archive),
                              MessageDialog::Kind::Error);
          SevenZipEngine::instance().shutdown();
          return 2;
        }
        jobs.append({QStringList{info.absoluteFilePath()}, archive});
      }
    }

    for (const Job &job : jobs)
    {
      for (const QString &source : job.sources)
      {
        if (QFileInfo(source).absoluteFilePath() == QFileInfo(job.archive).absoluteFilePath())
        {
          MessageDialog::show(nullptr, QStringLiteral("7zipLX"), QStringLiteral("Invalid output"),
                              QStringLiteral("The output archive would overwrite its input: %1")
                                  .arg(job.archive),
                              MessageDialog::Kind::Error);
          SevenZipEngine::instance().shutdown();
          return 2;
        }
      }
    }

    PromptDispatcher prompts;
    if (!parser.isSet(overwrite))
    {
      for (const Job &job : jobs)
      {
        if (QFileInfo::exists(job.archive) &&
            !prompts.confirm(QStringLiteral("Replace archive"),
                             QStringLiteral("%1 already exists. Replace it?").arg(job.archive), true))
        {
          SevenZipEngine::instance().shutdown();
          return 1;
        }
      }
    }

    OperationRunner runner(nullptr, &prompts);
    const OperationResult result = runner.run(
        QStringLiteral("Compressing"), [jobs, formatForArchive](ProgressState *state,
                                                                PromptDispatcher *prompts) {
          OperationResult total;
          total.ok = true;
          for (const Job &job : jobs)
          {
            CompressOptions options;
            options.archivePath = job.archive;
            options.formatIndex =
                SevenZipEngine::instance().formatIndex(formatForArchive(job.archive));
            options.level = 5;
            const OperationResult one = compressFiles(job.sources, options, state, prompts);
            total.files += one.files;
            total.unpackedSize += one.unpackedSize;
            total.messages += one.messages;
            if (!one.ok)
            {
              total.ok = false;
              total.error = one.error;
              return total;
            }
          }
          return total;
        });
    if (!result.ok && !result.canceled)
      MessageDialog::show(nullptr, QStringLiteral("Compression failed"),
                          QStringLiteral("Compression failed"), result.error,
                          MessageDialog::Kind::Error);
    SevenZipEngine::instance().shutdown();
    return result.ok ? 0 : (result.canceled ? 130 : 1);
  }

  if (parser.isSet(extractHere) || parser.isSet(extractTo))
  {
    const QStringList args = parser.positionalArguments();
    if (args.isEmpty())
    {
      MessageDialog::show(nullptr, QStringLiteral("7zipLX"), QStringLiteral("Missing archive"),
                          QStringLiteral("Specify an archive file to extract."),
                          MessageDialog::Kind::Error);
      SevenZipEngine::instance().shutdown();
      return 2;
    }
    struct ExtractJob
    {
      QString archive;
      QString outputDir;
    };
    QVector<ExtractJob> jobs;
    if (parser.isSet(extractTo))
    {
      const QString outputDir = parser.value(extractTo);
      for (const QString &archive : args)
        jobs.append({archive, outputDir});
    }
    else
    {
      for (const QString &archive : args)
        jobs.append({archive, QFileInfo(archive).absolutePath()});
    }
    PromptDispatcher prompts;
    OperationRunner runner(nullptr, &prompts);
    const OperationResult result = runner.run(
        QStringLiteral("Extracting"), [jobs](ProgressState *state, PromptDispatcher *prompts) {
          OperationResult total;
          total.ok = true;
          for (const ExtractJob &job : jobs)
          {
            ArchiveSession session;
            QString error;
            if (!session.open(job.archive, QString(), state, prompts, &error))
            {
              total.ok = false;
              total.error = error;
              return total;
            }
            ExtractOptions options;
            options.outputDir = job.outputDir;
            options.pathMode = 0;
            options.overwriteMode = 0;
            options.password = session.password();
            const OperationResult extracted = session.extract({}, options, state, prompts);
            session.close();
            total.files += extracted.files;
            total.folders += extracted.folders;
            total.messages += extracted.messages;
            if (!extracted.ok)
            {
              total.ok = false;
              total.error = extracted.error;
              return total;
            }
          }
          return total;
        });
    if (!result.ok && !result.canceled)
      MessageDialog::show(nullptr, QStringLiteral("Extraction failed"),
                          QStringLiteral("Extraction failed"), result.error,
                          MessageDialog::Kind::Error);
    SevenZipEngine::instance().shutdown();
    return result.ok ? 0 : (result.canceled ? 130 : 1);
  }

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
