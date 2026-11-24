#pragma once

#include <QString>
#include <QStringList>

#include "ArchiveSession.h"

class ProgressState;
class PromptDispatcher;

struct CompressOptions
{
  QString archivePath;
  QString archiveSubdir;         // folder inside the archive for new items
  int formatIndex = -1;          // -1: infer from archive extension
  int level = 5;
  int numThreads = -1;           // -1: automatic
  QString method;                // empty: format default
  quint64 dictionary = 0;        // 0: format default
  bool solid = false;
  quint64 volumeSize = 0;        // 0: single volume
  QString password;
  bool encryptHeaders = false;
};

OperationResult compressFiles(const QStringList &files,
                              const CompressOptions &options,
                              ProgressState *progress,
                              PromptDispatcher *prompts);

// Target of the quick "Add to <name>.7z" / "Add to <name>.zip" actions,
// following 7-Zip's naming: one item gives its own name (a file without its
// last extension, a folder as is); several items or no selection give the
// name of the folder that contains them.  With no selection, the current
// folder itself is compressed next to it.
struct QuickArchiveTarget
{
  QStringList inputs;
  QString archivePath;
  QString baseName;
};

QuickArchiveTarget quickArchiveTarget(const QStringList &selected, const QString &currentDir,
                                      const QString &extension);
