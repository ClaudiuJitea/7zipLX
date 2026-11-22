#include "HashResultDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

HashResultDialog::HashResultDialog(const QString &title, const QStringList &lines, QWidget *parent)
    : QDialog(parent)
{
  setWindowTitle(title);
  setWindowIcon(QIcon());
  resize(720, 340);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 22, 24, 18);
  layout->setSpacing(12);

  auto *heading = new QLabel(title, this);
  heading->setObjectName(QStringLiteral("dialogHeading"));
  layout->addWidget(heading);
  auto *sub = new QLabel(QStringLiteral("Select the values below or use Copy to put them on the clipboard."), this);
  sub->setObjectName(QStringLiteral("dialogSubheading"));
  sub->setWordWrap(true);
  layout->addWidget(sub);

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
