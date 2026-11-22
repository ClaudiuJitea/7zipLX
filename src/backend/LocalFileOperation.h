#pragma once

#include <QString>
#include <QStringList>

#include "backend/ArchiveSession.h"

class ProgressState;
class PromptDispatcher;

// Copies (or moves) local filesystem entries into targetDir.  Directories are
// merged, conflicting files ask the user, and symlinks are recreated instead
// of being followed.  Safe to call from a worker thread; PromptDispatcher
// marshals its dialogs back to the GUI thread.
OperationResult copyLocalItems(const QStringList &sources,
                               const QString &targetDir,
                               bool move,
                               ProgressState *progress,
                               PromptDispatcher *prompts);
