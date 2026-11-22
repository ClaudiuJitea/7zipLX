#pragma once

#include <QString>

#include "ArchiveSession.h"

class ProgressState;
class PromptDispatcher;

struct BenchmarkOptions
{
  QString method = QStringLiteral("LZMA2");
  quint64 dictionary = 16ULL << 20;
  int threads = 1;
  int iterations = 1;
  int timeMs = 0; // 0: use the engine default
};

struct BenchmarkResult
{
  OperationResult operation;
  QStringList log;
};

BenchmarkResult runBenchmark(const BenchmarkOptions &options,
                             ProgressState *progress,
                             PromptDispatcher *prompts);
