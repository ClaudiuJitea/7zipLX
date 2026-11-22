#pragma once

#include <QDialog>
#include <QStringList>

class ProgressState;
class QLabel;
class QProgressBar;
class QPlainTextEdit;
class QPushButton;
class QTimer;

class ProgressDialog : public QDialog
{
  Q_OBJECT
public:
  explicit ProgressDialog(ProgressState *state, const QString &title, QWidget *parent = nullptr);

protected:
  void closeEvent(QCloseEvent *event) override;

private slots:
  void updateUi();
  void cancelClicked();

private:
  ProgressState *m_state = nullptr;
  QLabel *m_title = nullptr;
  QLabel *m_status = nullptr;
  QLabel *m_file = nullptr;
  QLabel *m_stats = nullptr;
  QProgressBar *m_bytesBar = nullptr;
  QProgressBar *m_filesBar = nullptr;
  QPlainTextEdit *m_messages = nullptr;
  QPushButton *m_cancel = nullptr;
  QTimer *m_timer = nullptr;
  QStringList m_lastMessages;
};
