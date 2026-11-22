#include "ProgressState.h"

#include <QMutexLocker>

void ProgressState::setTitle(const QString &title)
{
  QMutexLocker lock(&m_mutex);
  m_title = title;
}

void ProgressState::setStatus(const QString &status)
{
  QMutexLocker lock(&m_mutex);
  m_status = status;
}

void ProgressState::setCurrentFile(const QString &file)
{
  QMutexLocker lock(&m_mutex);
  m_currentFile = file;
}

void ProgressState::setTotals(quint64 bytes, quint64 files)
{
  QMutexLocker lock(&m_mutex);
  m_bytesTotal = bytes;
  if (files != 0)
    m_filesTotal = files;
}

void ProgressState::setCompleted(quint64 bytes, quint64 files)
{
  QMutexLocker lock(&m_mutex);
  m_bytesDone = bytes;
  if (files != 0)
    m_filesDone = files;
}

void ProgressState::setRatio(quint64 inSize, quint64 outSize)
{
  QMutexLocker lock(&m_mutex);
  m_inSize = inSize;
  m_outSize = outSize;
}

void ProgressState::addMessage(const QString &message)
{
  if (message.isEmpty())
    return;
  QMutexLocker lock(&m_mutex);
  if (m_messages.size() >= 1000)
    m_messages.removeFirst();
  m_messages.append(message);
}

ProgressSnapshot ProgressState::snapshot() const
{
  QMutexLocker lock(&m_mutex);
  ProgressSnapshot s;
  s.title = m_title;
  s.status = m_status;
  s.currentFile = m_currentFile;
  s.bytesTotal = m_bytesTotal;
  s.bytesDone = m_bytesDone;
  s.filesTotal = m_filesTotal;
  s.filesDone = m_filesDone;
  s.inSize = m_inSize;
  s.outSize = m_outSize;
  s.messages = m_messages;
  s.canceled = m_canceled.load(std::memory_order_relaxed);
  return s;
}
