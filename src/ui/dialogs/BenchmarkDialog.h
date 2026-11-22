#pragma once

#include <QDialog>

#include "backend/BenchmarkOperation.h"

class QComboBox;
class QSpinBox;
class QPlainTextEdit;
class QPushButton;

class BenchmarkDialog : public QDialog
{
  Q_OBJECT
public:
  explicit BenchmarkDialog(QWidget *parent = nullptr);
  BenchmarkOptions options() const;
  void setLog(const QStringList &lines);
  void setRunning(bool running);

signals:
  void runRequested();

private:
  QComboBox *m_method = nullptr;
  QComboBox *m_dictionary = nullptr;
  QSpinBox *m_threads = nullptr;
  QSpinBox *m_iterations = nullptr;
  QPlainTextEdit *m_log = nullptr;
  QPushButton *m_run = nullptr;
};
