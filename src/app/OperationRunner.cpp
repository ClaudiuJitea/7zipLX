#include "OperationRunner.h"

#include "backend/ProgressState.h"
#include "backend/PromptDispatcher.h"
#include "ui/dialogs/ProgressDialog.h"

#include <QEventLoop>
#include <QThread>
#include <QTimer>

namespace {
constexpr int kProgressDialogDelayMs = 400;
}

class OperationThread : public QThread
{
public:
  std::function<OperationResult()> fn;
  OperationResult result;

  void run() override
  {
    try
    {
      result = fn ? fn() : OperationResult{};
    }
    catch (const std::exception &e)
    {
      result.error = QString::fromUtf8(e.what());
    }
    catch (...)
    {
      result.error = QObject::tr("Unknown error");
    }
  }
};

OperationRunner::OperationRunner(QWidget *parent, PromptDispatcher *prompts)
    : m_parent(parent), m_prompts(prompts)
{
}

OperationResult OperationRunner::run(const QString &title, const OperationFn &fn)
{
  ProgressState state;
  ProgressDialog dialog(&state, title, m_parent);
  OperationThread thread;
  thread.fn = [this, &fn, &state] { return fn(&state, m_prompts); };

  QEventLoop loop;
  QObject::connect(&thread, &QThread::finished, &loop, &QEventLoop::quit);
  thread.start();

  // Quick operations (a small "Add to <name>.7z", a test of a tiny archive)
  // should not flash a modal window.  Wait a moment first with user input
  // held back, so nothing can touch the main window while the worker runs,
  // and only show the progress dialog when the work takes longer.
  QTimer delay;
  delay.setSingleShot(true);
  QObject::connect(&delay, &QTimer::timeout, &loop, &QEventLoop::quit);
  delay.start(kProgressDialogDelayMs);
  loop.exec(QEventLoop::ExcludeUserInputEvents);
  delay.stop();

  if (!thread.isFinished())
  {
    dialog.show();
    loop.exec();
    dialog.hide();
  }
  thread.wait();
  return thread.result;
}
