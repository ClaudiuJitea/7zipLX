#pragma once

#include <QObject>
#include <functional>

#include "backend/ArchiveSession.h"
#include "ui/widgets/PanelTransfer.h"

class FilePanel;
class PromptDispatcher;
class ProgressState;

struct TransferContext
{
  PromptDispatcher *prompts = nullptr;
  std::function<FilePanel *(int)> panelById;
  std::function<FilePanel *()> leftPanel;
  std::function<FilePanel *()> rightPanel;
  std::function<OperationResult(const QString &,
                                const std::function<OperationResult(ProgressState *, PromptDispatcher *)> &)> run;
  std::function<bool(FilePanel *)> commitNested;
  std::function<void(const QString &)> setStatus;
  std::function<void()> updateActions;
};

class TransferController : public QObject
{
  Q_OBJECT
public:
  explicit TransferController(TransferContext context, QObject *parent = nullptr);

  bool performTransfer(const PanelTransfer &transfer, FilePanel *target,
                       const PanelDropTarget &targetPath, Qt::DropAction action,
                       bool confirmMove = false);

private:
  void selectTransferredItems(FilePanel *target, const PanelDropTarget &targetPath,
                              const PanelTransfer &transfer);

  TransferContext m_ctx;
};
