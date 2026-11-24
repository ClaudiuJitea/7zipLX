#include "CommandLine.h"

#include "OperationRunner.h"
#include "backend/ArchiveSession.h"
#include "backend/ArchiveTypes.h"
#include "backend/CompressOperation.h"
#include "backend/MessageDialog.h"
#include "backend/ProgressState.h"
#include "backend/PromptDispatcher.h"
#include "backend/SevenZipEngine.h"
#include "ui/dialogs/CompressDialog.h"
#include "ui/dialogs/ExtractDialog.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

QString tr(const char *text)
{
  return QCoreApplication::translate("CommandLine", text);
}

int exitCode(const OperationResult &result)
{
  if (result.ok)
    return 0;
  return result.canceled ? 130 : 1;
}

void showFailure(const QString &title, const OperationResult &result)
{
  if (!result.ok && !result.canceled && !result.error.isEmpty())
    MessageDialog::show(nullptr, title, title, result.error, MessageDialog::Kind::Error);
}

// Returns the absolute paths, or an empty list after reporting what is missing.
QStringList existingInputs(const QStringList &paths, const QString &what)
{
  QStringList out;
  QStringList missing;
  for (const QString &path : paths)
  {
    const QFileInfo info(path);
    if (info.exists() || info.isSymLink())
      out << info.absoluteFilePath();
    else
      missing << path;
  }
  if (paths.isEmpty())
  {
    MessageDialog::show(nullptr, QStringLiteral("7zipLX"), tr("Nothing selected"),
                        what, MessageDialog::Kind::Error);
    return {};
  }
  if (!missing.isEmpty())
  {
    MessageDialog::show(nullptr, QStringLiteral("7zipLX"), tr("Missing input"),
                        tr("These items do not exist:") + QStringLiteral("\n") +
                            missing.join(QLatin1Char('\n')),
                        MessageDialog::Kind::Error);
    return {};
  }
  return out;
}

QString formatForArchive(const QString &archive)
{
  const QString name = QFileInfo(archive).fileName().toLower();
  const QString suffix = QFileInfo(archive).suffix().toLower();
  if (suffix == QLatin1String("zip")) return QStringLiteral("zip");
  if (suffix == QLatin1String("tar")) return QStringLiteral("tar");
  if (suffix == QLatin1String("wim")) return QStringLiteral("wim");
  if (suffix == QLatin1String("gz") || suffix == QLatin1String("tgz")) return QStringLiteral("gzip");
  if (suffix == QLatin1String("bz2") || suffix == QLatin1String("tbz2")) return QStringLiteral("bzip2");
  if (suffix == QLatin1String("xz") || suffix == QLatin1String("txz")) return QStringLiteral("xz");
  if (suffix == QLatin1String("zst") || suffix == QLatin1String("tzst")) return QStringLiteral("zstd");
  Q_UNUSED(name)
  return QStringLiteral("7z");
}

int runCompress(const QStringList &inputs, const QString &archive, bool overwrite,
                PromptDispatcher *prompts)
{
  for (const QString &input : inputs)
  {
    if (QFileInfo(input).absoluteFilePath() == QFileInfo(archive).absoluteFilePath())
    {
      MessageDialog::show(nullptr, QStringLiteral("7zipLX"), tr("Invalid output"),
                          tr("The archive would overwrite one of its inputs:\n%1").arg(archive),
                          MessageDialog::Kind::Error);
      return 2;
    }
  }
  if (QFileInfo::exists(archive))
  {
    if (overwrite)
    {
      if (!QFile::remove(archive))
      {
        MessageDialog::show(nullptr, QStringLiteral("7zipLX"), tr("Cannot replace archive"),
                            archive, MessageDialog::Kind::Error);
        return 1;
      }
    }
    else if (!prompts->confirm(tr("Update archive"),
                               tr("%1 already exists.\nAdd the selected item(s) to it?")
                                   .arg(QFileInfo(archive).fileName())))
    {
      return 130;
    }
  }
  CompressOptions options;
  options.archivePath = archive;
  options.formatIndex = SevenZipEngine::instance().formatIndex(formatForArchive(archive));
  options.level = 5;
  OperationRunner runner(nullptr, prompts);
  const OperationResult result = runner.run(
      tr("Compressing to %1").arg(QFileInfo(archive).fileName()),
      [inputs, options](ProgressState *state, PromptDispatcher *p) {
        return compressFiles(inputs, options, state, p);
      });
  showFailure(tr("Compression failed"), result);
  return exitCode(result);
}

} // namespace

namespace CommandLine {

int extract(const QStringList &archivePaths, ExtractMode mode, const QString &directory,
            PromptDispatcher *prompts)
{
  const QStringList archives = existingInputs(archivePaths, tr("Select one or more archives to extract."));
  if (archives.isEmpty())
    return 2;

  ExtractOptions base;
  base.pathMode = 0;
  base.overwriteMode = 0;
  if (mode == ExtractMode::Directory)
    base.outputDir = QFileInfo(directory).absoluteFilePath();
  if (mode == ExtractMode::Ask)
  {
    ExtractDialog dialog;
    dialog.setArchivePath(archives.size() == 1
        ? archives.first()
        : tr("%1 archives").arg(archives.size()));
    const QString parent = QFileInfo(archives.first()).absolutePath();
    dialog.setDefaultOutputDir(archives.size() == 1
        ? parent + QLatin1Char('/') + archiveFolderName(archives.first())
        : parent);
    if (dialog.exec() != QDialog::Accepted)
      return 130;
    base = dialog.options();
  }

  OperationRunner runner(nullptr, prompts);
  const OperationResult result = runner.run(
      tr("Extracting"), [archives, base, mode](ProgressState *state, PromptDispatcher *p) {
        OperationResult total;
        total.ok = true;
        QStringList errors;
        for (const QString &archive : archives)
        {
          if (state && state->isCanceled())
          {
            total.ok = false;
            total.canceled = true;
            break;
          }
          ArchiveSession session;
          QString error;
          if (!session.open(archive, QString(), state, p, &error, base.password))
          {
            total.ok = false;
            errors << QFileInfo(archive).fileName() + QStringLiteral(": ") + error;
            continue;
          }
          ExtractOptions options = base;
          const QString parent = QFileInfo(archive).absolutePath();
          if (mode == ExtractMode::Here)
            options.outputDir = parent;
          else if (mode == ExtractMode::ToFolder)
            options.outputDir = parent + QLatin1Char('/') + archiveFolderName(archive);
          if (options.password.isEmpty())
            options.password = session.password();
          const OperationResult one = session.extract({}, options, state, p);
          session.close();
          total.files += one.files;
          total.folders += one.folders;
          total.messages += one.messages;
          if (one.canceled)
          {
            total.ok = false;
            total.canceled = true;
            break;
          }
          if (!one.ok)
          {
            total.ok = false;
            errors << QFileInfo(archive).fileName() + QStringLiteral(": ") + one.error;
          }
        }
        total.error = errors.join(QLatin1Char('\n'));
        return total;
      });
  showFailure(tr("Extraction failed"), result);
  return exitCode(result);
}

int test(const QStringList &archivePaths, PromptDispatcher *prompts)
{
  const QStringList archives = existingInputs(archivePaths, tr("Select one or more archives to test."));
  if (archives.isEmpty())
    return 2;
  OperationRunner runner(nullptr, prompts);
  const OperationResult result = runner.run(
      tr("Testing"), [archives](ProgressState *state, PromptDispatcher *p) {
        OperationResult total;
        total.ok = true;
        QStringList errors;
        for (const QString &archive : archives)
        {
          ArchiveSession session;
          QString error;
          if (!session.open(archive, QString(), state, p, &error))
          {
            total.ok = false;
            errors << QFileInfo(archive).fileName() + QStringLiteral(": ") + error;
            continue;
          }
          ExtractOptions options;
          options.testMode = true;
          options.password = session.password();
          const OperationResult one = session.extract({}, options, state, p);
          session.close();
          total.files += one.files;
          if (one.canceled)
          {
            total.ok = false;
            total.canceled = true;
            break;
          }
          if (!one.ok)
          {
            total.ok = false;
            errors << QFileInfo(archive).fileName() + QStringLiteral(": ") + one.error;
          }
        }
        total.error = errors.join(QLatin1Char('\n'));
        return total;
      });
  if (result.ok)
  {
    QStringList names;
    for (const QString &archive : archives)
      names << QFileInfo(archive).fileName();
    MessageDialog::show(nullptr, tr("Test completed"), tr("No errors found"),
                        tr("%1\n\n%2 file(s) tested.").arg(names.join(QLatin1Char('\n'))).arg(result.files),
                        MessageDialog::Kind::Information);
  }
  showFailure(tr("Test failed"), result);
  return exitCode(result);
}

int compressQuick(const QStringList &paths, const QString &format, bool overwrite,
                  PromptDispatcher *prompts)
{
  const QStringList inputs = existingInputs(paths, tr("Select the files or folders to compress."));
  if (inputs.isEmpty())
    return 2;
  const QString ext = format.compare(QLatin1String("zip"), Qt::CaseInsensitive) == 0
      ? QStringLiteral("zip") : QStringLiteral("7z");
  // Inputs from a file manager share one folder; name the archive like the
  // in-app action would for that folder.
  const QString folder = QFileInfo(inputs.first()).absolutePath();
  const QuickArchiveTarget target = quickArchiveTarget(inputs, folder, ext);
  return runCompress(target.inputs, target.archivePath, overwrite, prompts);
}

int compressTo(const QStringList &paths, const QString &archive, bool overwrite,
               PromptDispatcher *prompts)
{
  const QStringList inputs = existingInputs(paths, tr("Select the files or folders to compress."));
  if (inputs.isEmpty())
    return 2;
  return runCompress(inputs, QFileInfo(archive).absoluteFilePath(), overwrite, prompts);
}

int compressAsk(const QStringList &paths, PromptDispatcher *prompts)
{
  const QStringList inputs = existingInputs(paths, tr("Select the files or folders to compress."));
  if (inputs.isEmpty())
    return 2;
  CompressDialog dialog(inputs, QFileInfo(inputs.first()).absolutePath());
  if (dialog.exec() != QDialog::Accepted)
    return 130;
  const CompressOptions options = dialog.options();
  for (const QString &input : inputs)
  {
    if (QFileInfo(input).absoluteFilePath() == QFileInfo(options.archivePath).absoluteFilePath())
    {
      MessageDialog::show(nullptr, QStringLiteral("7zipLX"), tr("Invalid output"),
                          tr("The archive would overwrite one of its inputs:\n%1").arg(options.archivePath),
                          MessageDialog::Kind::Error);
      return 2;
    }
  }
  OperationRunner runner(nullptr, prompts);
  const OperationResult result = runner.run(
      tr("Compressing to %1").arg(QFileInfo(options.archivePath).fileName()),
      [inputs, options](ProgressState *state, PromptDispatcher *p) {
        return compressFiles(inputs, options, state, p);
      });
  showFailure(tr("Compression failed"), result);
  return exitCode(result);
}

} // namespace CommandLine
