#include "MessageDialog.h"

#include <QAbstractButton>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

MessageDialog::MessageDialog(QWidget *parent) : QDialog(parent)
{
  setObjectName(QStringLiteral("messageDialog"));
  setWindowIcon(QIcon());
  setMinimumWidth(480);
  setSizeGripEnabled(false);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 20, 24, 18);
  layout->setSpacing(12);

  m_heading = new QLabel(this);
  m_heading->setObjectName(QStringLiteral("messageHeading"));
  m_heading->setWordWrap(true);
  layout->addWidget(m_heading);

  m_text = new QLabel(this);
  m_text->setObjectName(QStringLiteral("messageText"));
  m_text->setWordWrap(true);
  m_text->setTextFormat(Qt::PlainText);
  m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_text);

  m_detail = new QLabel(this);
  m_detail->setObjectName(QStringLiteral("messageDetail"));
  m_detail->setWordWrap(true);
  m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_detail->hide();
  layout->addWidget(m_detail);

  layout->addStretch(1);

  m_buttons = new QDialogButtonBox(this);
  m_buttons->setCenterButtons(false);
  layout->addWidget(m_buttons);

  connect(m_buttons, &QDialogButtonBox::clicked, this, [this](QAbstractButton *button) {
    m_clicked = qobject_cast<QPushButton *>(button);
    if (m_buttons->buttonRole(button) == QDialogButtonBox::RejectRole)
      reject();
    else
      accept();
  });
}

void MessageDialog::setKind(Kind kind)
{
  QString color;
  switch (kind)
  {
    case Kind::Warning:
      color = QStringLiteral("#f0c674");
      break;
    case Kind::Error:
      color = QStringLiteral("#ff9d9d");
      break;
    case Kind::Question:
      color = QStringLiteral("#9dc1ff");
      break;
    case Kind::Information:
    default:
      break;
  }
  setProperty("messageKind",
              kind == Kind::Error ? QStringLiteral("error")
                                  : kind == Kind::Warning ? QStringLiteral("warning")
                                                          : kind == Kind::Question ? QStringLiteral("question")
                                                                                   : QStringLiteral("information"));
  if (!color.isEmpty())
    m_heading->setStyleSheet(QStringLiteral("color: %1;").arg(color));
}

void MessageDialog::setHeading(const QString &heading)
{
  m_heading->setText(heading);
  m_heading->setVisible(!heading.isEmpty());
}

void MessageDialog::setText(const QString &text, bool richText)
{
  m_text->setTextFormat(richText ? Qt::RichText : Qt::PlainText);
  m_text->setText(text);
  m_text->setVisible(!text.isEmpty());
}

void MessageDialog::setInformativeText(const QString &text)
{
  m_detail->setText(text);
  m_detail->setVisible(!text.isEmpty());
}

QPushButton *MessageDialog::addButton(const QString &text, QDialogButtonBox::ButtonRole role)
{
  auto *button = m_buttons->addButton(text, role);
  button->setAutoDefault(false);

  // QDialogButtonBox does not wrap.  Grow the dialog so six-button prompts
  // (overwrite: Yes/Yes to All/No/No to All/Auto Rename/Cancel) stay visible.
  m_buttons->ensurePolished();
  m_buttons->adjustSize();
  const int needed = m_buttons->sizeHint().width() + 56;
  if (needed > minimumWidth())
    setMinimumWidth(needed);
  return button;
}

void MessageDialog::setDefaultButton(QPushButton *button)
{
  if (!button)
    return;
  button->setAutoDefault(true);
  button->setDefault(true);
  button->setFocus();
}

void MessageDialog::show(QWidget *parent, const QString &title, const QString &heading,
                         const QString &text, Kind kind)
{
  MessageDialog dialog(parent);
  dialog.setWindowTitle(title);
  dialog.setKind(kind);
  dialog.setHeading(heading.isEmpty() ? title : heading);
  dialog.setText(text);
  auto *ok = dialog.addButton(QObject::tr("OK"), QDialogButtonBox::AcceptRole);
  dialog.setDefaultButton(ok);
  dialog.exec();
}

bool MessageDialog::confirm(QWidget *parent, const QString &title, const QString &heading,
                            const QString &text, bool dangerous)
{
  MessageDialog dialog(parent);
  dialog.setWindowTitle(title);
  dialog.setKind(Kind::Question);
  dialog.setHeading(heading.isEmpty() ? title : heading);
  dialog.setText(text);
  auto *yes = dialog.addButton(QObject::tr("Yes"), QDialogButtonBox::AcceptRole);
  auto *no = dialog.addButton(QObject::tr("No"), QDialogButtonBox::RejectRole);
  dialog.setDefaultButton(dangerous ? no : yes);
  dialog.exec();
  return dialog.clickedButton() == yes;
}
