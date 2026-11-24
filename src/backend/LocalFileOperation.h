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

// Volume names produced by splitFile(): "<source>.001", "<source>.002", ...
QString splitVolumeName(const QString &source, int part);
// Number of volumes splitFile() writes for a file of this size (at least 1).
int splitVolumeCount(qint64 fileSize, qint64 volumeSize);

// Splits a file into fixed-size volumes next to it, streaming in small
// buffers.  Fails before writing anything if a volume already exists.
OperationResult splitFile(const QString &source, qint64 volumeSize, ProgressState *progress);

// Joins "<base>.001", "<base>.002", ... (first volume given) into output.
// Fails if output already exists or is one of the volumes.
OperationResult combineFiles(const QString &firstVolume, const QString &output,
                             ProgressState *progress);
