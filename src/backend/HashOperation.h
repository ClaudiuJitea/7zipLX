#pragma once

#include <QString>
#include <QStringList>

#include "ArchiveSession.h"

class ProgressState;
class PromptDispatcher;

struct HashOptions
{
  QStringList methods; // CRC32, SHA256, ...
};

struct HashResult
{
  OperationResult operation;
  QStringList lines;
};

HashResult hashFiles(const QStringList &files,
                     const HashOptions &options,
                     ProgressState *progress,
                     PromptDispatcher *prompts);
