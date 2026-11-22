#pragma once

#include <QString>
#include <QStringList>

#include "ArchiveSession.h"

class ProgressState;
class PromptDispatcher;

struct CompressOptions
{
  QString archivePath;
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
