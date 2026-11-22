#pragma once

#include <QString>
#include <QStringList>
#include <QMutex>
#include <atomic>

struct ProgressSnapshot
{
  QString title;
  QString status;
  QString currentFile;
  quint64 bytesTotal = 0;
  quint64 bytesDone = 0;
  quint64 filesTotal = 0;
  quint64 filesDone = 0;
  quint64 inSize = 0;
  quint64 outSize = 0;
  QStringList messages;
  bool canceled = false;
};

class ProgressState
{
public:
  ProgressState() = default;
  ProgressState(const ProgressState &) = delete;
  ProgressState &operator=(const ProgressState &) = delete;

  void cancel() { m_canceled.store(true, std::memory_order_relaxed); }
  bool isCanceled() const { return m_canceled.load(std::memory_order_relaxed); }

  void setTitle(const QString &title);
  void setStatus(const QString &status);
  void setCurrentFile(const QString &file);
  void setTotals(quint64 bytes, quint64 files = 0);
  void setCompleted(quint64 bytes, quint64 files = 0);
  void setRatio(quint64 inSize, quint64 outSize);
  void addMessage(const QString &message);

  ProgressSnapshot snapshot() const;

private:
  mutable QMutex m_mutex;
  std::atomic<bool> m_canceled{false};
  QString m_title;
  QString m_status;
  QString m_currentFile;
  quint64 m_bytesTotal = 0;
  quint64 m_bytesDone = 0;
  quint64 m_filesTotal = 0;
  quint64 m_filesDone = 0;
  quint64 m_inSize = 0;
  quint64 m_outSize = 0;
  QStringList m_messages;
};
