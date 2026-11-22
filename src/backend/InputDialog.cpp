#include "InputDialog.h"

#include "ui/dialogs/DialogLayout.h"

#include <QCheckBox>
#include <QIcon>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

InputDialog::InputDialog(QWidget *parent) : QDialog(parent)
{
  setObjectName(QStringLiteral("inputDialog"));
  setWindowIcon(QIcon());
  setMinimumWidth(460);

  m_layout = new QVBoxLayout(this);
  m_layout->setContentsMargins(24, 20, 24, 18);
  m_layout->setSpacing(12);

  m_heading = new QLabel(this);
  m_heading->setObjectName(QStringLiteral("messageHeading"));
  m_heading->setWordWrap(true);
  m_layout->addWidget(m_heading);

  m_subheading = new QLabel(this);
  m_subheading->setObjectName(QStringLiteral("dialogSubheading"));
  m_subheading->setWordWrap(true);
  m_subheading->hide();
  m_layout->addWidget(m_subheading);

  m_form = new QFormLayout();
  configureDialogForm(m_form);
  m_layout->addLayout(m_form);
  m_layout->addStretch(1);

  m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  m_layout->addWidget(m_buttons);
  connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  if (QPushButton *ok = m_buttons->button(QDialogButtonBox::Ok))
  {
    ok->setDefault(true);
    ok->setAutoDefault(true);
  }
}

void InputDialog::setHeading(const QString &heading)
{
  m_heading->setText(heading);
  m_heading->setVisible(!heading.isEmpty());
}

void InputDialog::setSubheading(const QString &subheading)
{
  m_subheading->setText(subheading);
  m_subheading->setVisible(!subheading.isEmpty());
}

QLineEdit *InputDialog::addLineEdit(const QString &label, const QString &value, bool password)
{
  auto *edit = new QLineEdit(value, this);
  if (password)
    edit->setEchoMode(QLineEdit::Password);
  m_form->addRow(label, edit);
  return edit;
}

QPlainTextEdit *InputDialog::addPlainTextEdit(const QString &label, const QString &value)
{
  auto *edit = new QPlainTextEdit(value, this);
  edit->setMinimumHeight(130);
  edit->setTabChangesFocus(true);
  m_form->addRow(label, edit);
  return edit;
}

QComboBox *InputDialog::addComboBox(const QString &label, const QStringList &items, int current)
{
  auto *combo = new QComboBox(this);
  combo->addItems(items);
  if (current >= 0 && current < combo->count())
    combo->setCurrentIndex(current);
  m_form->addRow(label, combo);
  return combo;
}

QSpinBox *InputDialog::addSpinBox(const QString &label, int minimum, int maximum, int value)
{
  auto *spin = new QSpinBox(this);
  spin->setRange(minimum, maximum);
  spin->setValue(value);
  m_form->addRow(label, spin);
  return spin;
}

bool InputDialog::getText(QWidget *parent, const QString &title, const QString &heading,
                          const QString &label, QString *value, bool password)
{
  InputDialog dialog(parent);
  dialog.setWindowTitle(title);
  dialog.setHeading(heading.isEmpty() ? title : heading);
  QLineEdit *edit = dialog.addLineEdit(label, value ? *value : QString(), password);
  if (password)
  {
    auto *show = new QCheckBox(QObject::tr("Show password"), &dialog);
    dialog.m_form->addRow(QString(), show);
    QObject::connect(show, &QCheckBox::toggled, edit, [edit](bool checked) {
      edit->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
    });
  }
  edit->selectAll();
  if (dialog.exec() != QDialog::Accepted)
    return false;
  if (value)
    *value = edit->text();
  return true;
}

bool InputDialog::getMultiLineText(QWidget *parent, const QString &title, const QString &heading,
                                   const QString &label, QString *value)
{
  InputDialog dialog(parent);
  dialog.setWindowTitle(title);
  dialog.setHeading(heading.isEmpty() ? title : heading);
  QPlainTextEdit *edit = dialog.addPlainTextEdit(label, value ? *value : QString());
  if (dialog.exec() != QDialog::Accepted)
    return false;
  if (value)
    *value = edit->toPlainText();
  return true;
}

bool InputDialog::getItem(QWidget *parent, const QString &title, const QString &heading,
                          const QString &label, const QStringList &items, int current,
                          QString *value)
{
  InputDialog dialog(parent);
  dialog.setWindowTitle(title);
  dialog.setHeading(heading.isEmpty() ? title : heading);
  QComboBox *combo = dialog.addComboBox(label, items, current);
  if (dialog.exec() != QDialog::Accepted)
    return false;
  if (value)
    *value = combo->currentText();
  return true;
}

bool InputDialog::getInt(QWidget *parent, const QString &title, const QString &heading,
                         const QString &label, int minimum, int maximum, int value,
                         int *result)
{
  InputDialog dialog(parent);
  dialog.setWindowTitle(title);
  dialog.setHeading(heading.isEmpty() ? title : heading);
  QSpinBox *spin = dialog.addSpinBox(label, minimum, maximum, value);
  spin->selectAll();
  if (dialog.exec() != QDialog::Accepted)
    return false;
  if (result)
    *result = spin->value();
  return true;
}
