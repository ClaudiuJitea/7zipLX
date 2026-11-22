#include "ProgressDialog.h"
#include "DialogLayout.h"
#include "backend/ProgressState.h"

#include <QCloseEvent>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QHBoxLayout>
#include <QLocale>

ProgressDialog::ProgressDialog(ProgressState *state, const QString &title, QWidget *parent)
    : QDialog(parent), m_state(state)
{
  setWindowTitle(title);
  setWindowIcon(QIcon());
  setWindowModality(Qt::ApplicationModal);
  setMinimumWidth(600);
  setSizeGripEnabled(true);
  setObjectName(QStringLiteral("progressDialog"));

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 22, 24, 18);
  layout->setSpacing(12);

  m_title = new QLabel(title, this);
  m_title->setObjectName(QStringLiteral("dialogHeading"));
  layout->addWidget(m_title);

  m_status = new QLabel(QStringLiteral("Preparing..."), this);
  m_status->setObjectName(QStringLiteral("progressStatus"));
  m_status->setWordWrap(true);
  layout->addWidget(m_status);

  m_file = new QLabel(this);
  m_file->setObjectName(QStringLiteral("progressFile"));
  m_file->setWordWrap(true);
  m_file->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_file);

  auto *form = new QFormLayout();
  configureDialogForm(form);
  m_bytesBar = new QProgressBar(this);
  m_bytesBar->setRange(0, 1000);
  m_bytesBar->setValue(0);
  form->addRow(QStringLiteral("Bytes:"), m_bytesBar);
  m_filesBar = new QProgressBar(this);
  m_filesBar->setRange(0, 1000);
  m_filesBar->setValue(0);
  form->addRow(QStringLiteral("Files:"), m_filesBar);
  m_stats = new QLabel(this);
  m_stats->setObjectName(QStringLiteral("progressStats"));
  form->addRow(QString(), m_stats);
  layout->addLayout(form);

  m_messages = new QPlainTextEdit(this);
  m_messages->setObjectName(QStringLiteral("progressMessages"));
  m_messages->setReadOnly(true);
  m_messages->setMaximumBlockCount(1000);
  m_messages->setMinimumHeight(130);
  m_messages->setPlaceholderText(QStringLiteral("Operation details will appear here."));
  layout->addWidget(m_messages, 1);

  auto *buttons = new QHBoxLayout();
  buttons->setContentsMargins(0, 4, 0, 0);
  buttons->addStretch();
  m_cancel = new QPushButton(QStringLiteral("Cancel"), this);
  m_cancel->setAutoDefault(false);
  m_cancel->setMinimumWidth(100);
  buttons->addWidget(m_cancel);
  layout->addLayout(buttons);

  connect(m_cancel, &QPushButton::clicked, this, &ProgressDialog::cancelClicked);
  m_timer = new QTimer(this);
  m_timer->setInterval(100);
  connect(m_timer, &QTimer::timeout, this, &ProgressDialog::updateUi);
  m_timer->start();
  updateUi();
}

void ProgressDialog::cancelClicked()
{
  if (m_state)
    m_state->cancel();
  m_cancel->setEnabled(false);
  m_cancel->setText(QStringLiteral("Canceling..."));
  m_status->setText(QStringLiteral("Canceling..."));
}

void ProgressDialog::closeEvent(QCloseEvent *event)
{
  if (m_state)
    m_state->cancel();
  event->ignore();
}

void ProgressDialog::updateUi()
{
  if (!m_state)
    return;
  const ProgressSnapshot s = m_state->snapshot();
  if (!s.status.isEmpty())
    m_status->setText(s.status);
  m_file->setText(s.currentFile);
  m_file->setToolTip(s.currentFile);

  auto setBar = [](QProgressBar *bar, quint64 done, quint64 total) {
    if (total == 0)
    {
      bar->setRange(0, 0);
    }
    else
    {
      bar->setRange(0, 1000);
      const double ratio = double(done) / double(total);
      bar->setValue(int(qBound(0.0, ratio, 1.0) * 1000.0));
    }
  };
  setBar(m_bytesBar, s.bytesDone, s.bytesTotal);
  setBar(m_filesBar, s.filesDone, s.filesTotal);

  QString stats;
  if (s.bytesTotal)
    stats += QStringLiteral("%1 / %2").arg(QLocale().formattedDataSize(qint64(s.bytesDone), 1, QLocale::DataSizeTraditionalFormat),
                                           QLocale().formattedDataSize(qint64(s.bytesTotal), 1, QLocale::DataSizeTraditionalFormat));
  else if (s.bytesDone)
    stats += QLocale().formattedDataSize(qint64(s.bytesDone), 1, QLocale::DataSizeTraditionalFormat);
  if (s.filesTotal)
    stats += QStringLiteral("   %1 / %2 files").arg(s.filesDone).arg(s.filesTotal);
  else if (s.filesDone)
    stats += QStringLiteral("   %1 files").arg(s.filesDone);
  if (s.inSize || s.outSize)
    stats += QStringLiteral("   in/out: %1 / %2").arg(s.inSize).arg(s.outSize);
  m_stats->setText(stats);

  if (s.messages != m_lastMessages)
  {
    m_lastMessages = s.messages;
    m_messages->setPlainText(s.messages.join(QLatin1Char('\n')));
    m_messages->verticalScrollBar()->setValue(m_messages->verticalScrollBar()->maximum());
  }
}
