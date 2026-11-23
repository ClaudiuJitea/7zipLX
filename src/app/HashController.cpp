#include "HashController.h"

#include "OperationRunner.h"
#include "backend/HashOperation.h"
#include "backend/InputDialog.h"
#include "backend/PromptDispatcher.h"
#include "backend/ProgressState.h"
#include "models/ArchiveFolderModel.h"
#include "models/LocalFsModel.h"
#include "ui/dialogs/HashResultDialog.h"
#include "ui/widgets/FilePanel.h"

HashController::HashController(QWidget *parent, OperationRunner *runner, PromptDispatcher *prompts)
    : QObject(parent), m_parent(parent), m_runner(runner), m_prompts(prompts)
{
}

void HashController::promptAndHash(FilePanel *panel)
{
  if (!panel)
    return;
  const QStringList methods = {
      QStringLiteral("CRC32"), QStringLiteral("SHA1"), QStringLiteral("SHA256"),
      QStringLiteral("SHA512"), QStringLiteral("MD5"), QStringLiteral("SHA3-256"),
      QStringLiteral("BLAKE2sp"), QStringLiteral("XXH64"), QStringLiteral("CRC64")};
  QString method;
  if (!InputDialog::getItem(m_parent, HashController::tr("Hash"), HashController::tr("Hash"), HashController::tr("Hash method:"), methods, 2,
                            &method) ||
      method.isEmpty())
    return;
  hashSelection(panel, method);
}

void HashController::hashSelection(FilePanel *panel, const QString &method)
{
  if (!panel)
    return;

  HashOptions options;
  options.methods << method;

  if (!panel->isArchive())
  {
    QStringList files = panel->selectedLocalPaths();
    if (files.isEmpty())
    {
      const int row = panel->currentRow();
      if (row >= 0 && panel->localModel())
        files << panel->localModel()->filePath(row);
      else
        files << panel->localPath();
    }
    const OperationResult hashed = m_runner->run(
        HashController::tr("Hashing"), [files, options](ProgressState *state, PromptDispatcher *prompts) {
          const HashResult one = hashFiles(files, options, state, prompts);
          OperationResult result = one.operation;
          result.details = one.lines;
          return result;
        });
    if (!hashed.ok)
    {
      if (!hashed.canceled && !hashed.error.isEmpty())
        m_prompts->showError(HashController::tr("Hashing failed"), hashed.error);
      return;
    }
    showResult(method, HashController::tr("%1 hash").arg(method), hashed.details);
    return;
  }

  ArchiveSession *session =
      panel->archiveModel() ? panel->archiveModel()->session() : nullptr;
  if (!session)
    return;
  QVector<int> rows = panel->selectedArchiveRows();
  if (rows.isEmpty())
  {
    const int row = panel->currentRow();
    if (row >= 0)
      rows << row;
  }

  QStringList lines;
  const OperationResult result = m_runner->run(
      HashController::tr("Hashing archive items"),
      [session, rows, options, &lines](ProgressState *state, PromptDispatcher *prompts) {
        return session->hashItems(rows, options.methods, &lines, state, prompts);
      });
  if (!result.ok)
  {
    if (!result.canceled && !result.error.isEmpty())
      m_prompts->showError(HashController::tr("Hashing failed"), result.error);
    return;
  }
  showResult(method, HashController::tr("%1 hash").arg(method), lines.isEmpty() ? result.details : lines);
}

void HashController::showResult(const QString &method, const QString &title,
                                const QStringList &lines)
{
  Q_UNUSED(method)
  HashResultDialog dialog(title,
                          lines.isEmpty() ? QStringList{HashController::tr("Hashing completed.")} : lines,
                          m_parent);
  dialog.exec();
}
