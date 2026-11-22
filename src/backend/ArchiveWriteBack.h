#pragma once

#include <QString>

#include "backend/ArchiveSession.h"

class ProgressState;
class PromptDispatcher;

// True for 7z/zip split-volume paths such as archive.7z.001.
bool isSplitVolumePath(const QString &path);

// Single-file compressors (gzip, bzip2, xz, ...) cannot be updated in place by
// the 7-Zip update engine, but they can be rebuilt from the replacement file.
bool isSingleFileCompressorFormat(const QString &formatName);

// Preflight check used before allowing edits inside a nested archive.
bool canReplaceArchiveEntry(const QString &archivePath, const QString &archiveSubdir,
                            const QString &formatName, QString *reason);

// Replace one entry of an archive with the contents of replacementFile.  The
// replacement is identified by its base name and is placed inside archiveSubdir.
// Update-capable formats (7z, zip, tar, ...) are patched in place; single-file
// compressors are rebuilt atomically.
OperationResult replaceArchiveEntry(const QString &archivePath, const QString &archiveSubdir,
                                    const QString &replacementFile, const QString &formatName,
                                    const QString &password, ProgressState *progress,
                                    PromptDispatcher *prompts);
