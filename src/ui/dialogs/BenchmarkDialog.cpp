#include "BenchmarkDialog.h"
#include "DialogLayout.h"

#include <QComboBox>
#include <QIcon>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

BenchmarkDialog::BenchmarkDialog(QWidget *parent) : QDialog(parent)
{
  setWindowTitle(QStringLiteral("Benchmark"));
  setWindowIcon(QIcon());
  setMinimumSize(640, 500);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 22, 24, 18);
  layout->setSpacing(12);
  auto *heading = new QLabel(QStringLiteral("Benchmark"), this);
  heading->setObjectName(QStringLiteral("dialogHeading"));
  layout->addWidget(heading);
  auto *sub = new QLabel(QStringLiteral("Measure 7-Zip compression and decompression speed for a method and dictionary size."), this);
  sub->setObjectName(QStringLiteral("dialogSubheading"));
  sub->setWordWrap(true);
  layout->addWidget(sub);

  auto *form = new QFormLayout();
  configureDialogForm(form);
  m_method = new QComboBox(this);
  m_method->addItems({QStringLiteral("LZMA2"), QStringLiteral("LZMA")});
  form->addRow(QStringLiteral("Method:"), m_method);
  m_dictionary = new QComboBox(this);
  const struct { const char *label; quint64 value; } dicts[] = {
      {"1 MiB", 1ULL << 20}, {"4 MiB", 4ULL << 20}, {"16 MiB", 16ULL << 20},
      {"64 MiB", 64ULL << 20}, {"256 MiB", 256ULL << 20}};
  for (const auto &d : dicts)
    m_dictionary->addItem(QString::fromLatin1(d.label), QVariant::fromValue<qulonglong>(d.value));
  m_dictionary->setCurrentIndex(2);
  form->addRow(QStringLiteral("Dictionary:"), m_dictionary);
  m_threads = new QSpinBox(this);
  m_threads->setRange(1, 256);
  m_threads->setValue(1);
  form->addRow(QStringLiteral("Threads:"), m_threads);
  m_iterations = new QSpinBox(this);
  m_iterations->setRange(1, 20);
  m_iterations->setValue(1);
  form->addRow(QStringLiteral("Iterations:"), m_iterations);
  layout->addLayout(form);

  m_log = new QPlainTextEdit(this);
  m_log->setReadOnly(true);
  m_log->setPlaceholderText(QStringLiteral("Benchmark output will appear here."));
  layout->addWidget(m_log, 1);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  m_run = buttons->addButton(QStringLiteral("Run Benchmark"), QDialogButtonBox::ActionRole);
  m_run->setDefault(true);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_run, &QPushButton::clicked, this, [this] {
    setRunning(true);
    m_log->setPlainText(QStringLiteral("Running benchmark..."));
    emit runRequested();
  });
}

BenchmarkOptions BenchmarkDialog::options() const
{
  BenchmarkOptions o;
  o.method = m_method->currentText();
  o.dictionary = m_dictionary->currentData().toULongLong();
  o.threads = m_threads->value();
  o.iterations = m_iterations->value();
  return o;
}

void BenchmarkDialog::setLog(const QStringList &lines)
{
  m_log->setPlainText(lines.join(QLatin1Char('\n')));
}

void BenchmarkDialog::setRunning(bool running)
{
  m_run->setEnabled(!running);
  m_method->setEnabled(!running);
  m_dictionary->setEnabled(!running);
  m_threads->setEnabled(!running);
  m_iterations->setEnabled(!running);
}
