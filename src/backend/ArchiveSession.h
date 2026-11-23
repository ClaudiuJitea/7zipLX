#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QDateTime>
#include <memory>

class ProgressState;
class PromptDispatcher;

struct ArchiveEntryInfo
{
  QString name;
  QString path;
  QString type;
  QString method;
  QString comment;
  quint64 size = 0;
  quint64 packedSize = 0;
  QDateTime modified;
  QDateTime created;
  QDateTime accessed;
  quint32 attrib = 0;
  quint32 crc = 0;
  bool isDir = false;
  bool isEncrypted = false;
  bool hasCrc = false;
  bool isAltStream = false;
};

struct ExtractOptions
{
  QString outputDir;
  bool testMode = false;
  QString password;
  int pathMode = 0;      // 0 full, 1 current, 2 no paths, 3 absolute
  int overwriteMode = 0; // 0 ask, 1 overwrite, 2 skip, 3 rename, 4 rename existing
  bool includeAltStreams = false;
};

struct OperationResult
{
  bool ok = false;
  bool canceled = false;
  QString error;
  QStringList messages;
  QStringList details;
  quint64 files = 0;
  quint64 folders = 0;
  quint64 unpackedSize = 0;
  quint64 packedSize = 0;
};

class ArchiveSession
{
public:
  ArchiveSession();
  ~ArchiveSession();
  ArchiveSession(const ArchiveSession &) = delete;
  ArchiveSession &operator=(const ArchiveSession &) = delete;

  bool open(const QString &path,
            const QString &formatHint,
            ProgressState *progress,
            PromptDispatcher *prompts,
            QString *error,
            const QString &password = QString());
  void close();

  bool isOpen() const;
  QString archivePath() const;
  QString formatName() const;
  QString password() const;
  QString currentPath() const;
  QString relativePath() const;
  QStringList breadcrumbs() const;

  int itemCount() const;
  ArchiveEntryInfo item(int index) const;

  bool enter(int index);
  bool enterPath(const QString &relativePath);
  bool up();
  bool root();
  bool setFlatMode(bool flat);
  OperationResult setComment(int index, const QString &comment, ProgressState *progress, PromptDispatcher *prompts = nullptr);

  OperationResult extract(const QVector<int> &selectedIndices,
                          const QString &outputDir,
                          bool testMode,
                          ProgressState *progress,
                          PromptDispatcher *prompts,
                          const QString &password = QString());

  OperationResult extract(const QVector<int> &selectedIndices,
                          const ExtractOptions &options,
                          ProgressState *progress,
                          PromptDispatcher *prompts);

  // Hash selected items without extracting them to disk.  The archive engine
  // streams each item through the hashers in memory; only the progress and
  // password prompts touch the UI.
  OperationResult hashItems(const QVector<int> &selectedIndices,
                            const QStringList &methods,
                            QStringList *lines,
                            ProgressState *progress,
                            PromptDispatcher *prompts);

  OperationResult deleteItems(const QVector<int> &selectedIndices,
                              ProgressState *progress,
                              PromptDispatcher *prompts = nullptr);
  OperationResult renameItem(int index, const QString &newName,
                             ProgressState *progress,
                             PromptDispatcher *prompts = nullptr);
  OperationResult createFolder(const QString &name, ProgressState *progress,
                               PromptDispatcher *prompts = nullptr);

private:
  struct Impl;
  std::unique_ptr<Impl> d;
  bool rebindAfterEdit(const QStringList &pathNames);
};
