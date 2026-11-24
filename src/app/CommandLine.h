#pragma once

#include <QString>
#include <QStringList>

class PromptDispatcher;

// One-shot jobs started from the command line, typically by a file manager
// context menu.  Each shows the usual progress, prompt and result dialogs and
// returns a process exit code (0 success, 1 failure, 2 bad input, 130 canceled).
namespace CommandLine {

enum class ExtractMode
{
  Here,        // into the folder that contains the archive
  ToFolder,    // into "<folder>/<archive name>/"
  Directory,   // into an explicit directory
  Ask          // show the Extract dialog
};

int extract(const QStringList &archives, ExtractMode mode, const QString &directory,
            PromptDispatcher *prompts);
int test(const QStringList &archives, PromptDispatcher *prompts);

// "Add to <name>.<format>": all inputs go into one archive named like the
// in-app quick action does.  An existing archive is updated after asking,
// unless overwrite is set, which replaces it.
int compressQuick(const QStringList &inputs, const QString &format, bool overwrite,
                  PromptDispatcher *prompts);
// Every input into an explicit archive path.
int compressTo(const QStringList &inputs, const QString &archive, bool overwrite,
               PromptDispatcher *prompts);
// Shows the "Add to Archive" dialog for the inputs.
int compressAsk(const QStringList &inputs, PromptDispatcher *prompts);

} // namespace CommandLine
