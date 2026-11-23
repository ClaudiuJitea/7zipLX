#pragma once

#include <QString>
#include <functional>

#include "backend/ArchiveSession.h"

class QWidget;
class ProgressState;
class PromptDispatcher;

// Runs one archive operation on a worker thread while showing the themed
// progress dialog and keeping the GUI responsive.  Every engine entry point
// that can take measurable time must go through this class (or the
// controllers built on top of it) instead of being called from the GUI thread.
class OperationRunner
{
public:
  using OperationFn = std::function<OperationResult(ProgressState *, PromptDispatcher *)>;

  OperationRunner(QWidget *parent, PromptDispatcher *prompts);

  OperationResult run(const QString &title, const OperationFn &fn);

  QWidget *parentWidget() const { return m_parent; }
  PromptDispatcher *prompts() const { return m_prompts; }

private:
  QWidget *m_parent = nullptr;
  PromptDispatcher *m_prompts = nullptr;
};
