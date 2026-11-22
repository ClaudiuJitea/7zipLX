#include "HashResultDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

HashResultDialog::HashResultDialog(const QString &title, const QStringList &lines, QWidget *parent)
    : QDialog(parent)
{
  setWindowTitle(title);
  resize(680, 300);
  auto *layout = new QVBoxLayout(this);
  m_text = new QPlainTextEdit(this);
  m_text->setReadOnly(true);
  m_text->setPlainText(lines.join(QLatin1Char('\n')));
  layout->addWidget(m_text);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  auto *copy = buttons->addButton(QStringLiteral("Copy"), QDialogButtonBox::ActionRole);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(copy, &QPushButton::clicked, this, [this] {
    QApplication::clipboard()->setText(m_text->toPlainText());
  });
}
