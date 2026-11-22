#include "ExtractDialog.h"
#include "DialogLayout.h"

#include <QCheckBox>
#include <QIcon>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

ExtractDialog::ExtractDialog(QWidget *parent) : QDialog(parent)
{
  setWindowTitle(QStringLiteral("Extract"));
  setWindowIcon(QIcon());
  setMinimumWidth(560);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 22, 24, 18);
  layout->setSpacing(12);
  auto *heading = new QLabel(QStringLiteral("Extract Archive"), this);
  heading->setObjectName(QStringLiteral("dialogHeading"));
  layout->addWidget(heading);
  auto *sub = new QLabel(QStringLiteral("Choose a destination and extraction options."), this);
  sub->setObjectName(QStringLiteral("dialogSubheading"));
  layout->addWidget(sub);
  auto *info = new QLabel(this);
  info->setObjectName(QStringLiteral("dialogInfo"));
  info->setWordWrap(true);
  info->hide();
  layout->addWidget(info);

  auto *form = new QFormLayout();
  configureDialogForm(form);
  auto *dirRow = new QHBoxLayout();
  dirRow->setContentsMargins(0, 0, 0, 0);
  dirRow->setSpacing(8);
  m_dir = new QLineEdit(this);
  m_dir->setText(QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
  auto *browseButton = new QPushButton(QStringLiteral("Browse..."), this);
  dirRow->addWidget(m_dir, 1);
  dirRow->addWidget(browseButton);
  form->addRow(QStringLiteral("Extract to:"), dirRow);

  m_pathMode = new QComboBox(this);
  m_pathMode->addItem(QStringLiteral("Full paths"), 0);
  m_pathMode->addItem(QStringLiteral("Current folder paths"), 1);
  m_pathMode->addItem(QStringLiteral("No paths"), 2);
  m_pathMode->addItem(QStringLiteral("Absolute paths"), 3);
  form->addRow(QStringLiteral("Path mode:"), m_pathMode);

  m_overwrite = new QComboBox(this);
  m_overwrite->addItem(QStringLiteral("Ask before overwrite"), 0);
  m_overwrite->addItem(QStringLiteral("Overwrite"), 1);
  m_overwrite->addItem(QStringLiteral("Skip existing"), 2);
  m_overwrite->addItem(QStringLiteral("Auto rename"), 3);
  m_overwrite->addItem(QStringLiteral("Rename existing"), 4);
  form->addRow(QStringLiteral("Overwrite:"), m_overwrite);

  m_password = new QLineEdit(this);
  m_password->setEchoMode(QLineEdit::Password);
  form->addRow(QStringLiteral("Password:"), m_password);
  m_showPassword = new QCheckBox(QStringLiteral("Show password"), this);
  form->addRow(QString(), m_showPassword);
  layout->addLayout(form);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(browseButton, &QPushButton::clicked, this, &ExtractDialog::browse);
  connect(m_showPassword, &QCheckBox::toggled, this, &ExtractDialog::updatePasswordState);
  setArchivePath(QString());
}

void ExtractDialog::setArchivePath(const QString &path)
{
  m_archivePath = path;
  setWindowTitle(path.isEmpty() ? QStringLiteral("Extract") : QStringLiteral("Extract - %1").arg(QFileInfo(path).fileName()));
  if (auto *label = findChild<QLabel *>(QStringLiteral("dialogInfo")))
  {
    label->setText(path);
    label->setVisible(!path.isEmpty());
  }
}

void ExtractDialog::setDefaultOutputDir(const QString &dir)
{
  if (!dir.isEmpty())
    m_dir->setText(dir);
}

void ExtractDialog::browse()
{
  const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select output folder"), m_dir->text());
  if (!dir.isEmpty())
    m_dir->setText(dir);
}

void ExtractDialog::updatePasswordState()
{
  m_password->setEchoMode(m_showPassword->isChecked() ? QLineEdit::Normal : QLineEdit::Password);
}

ExtractOptions ExtractDialog::options() const
{
  ExtractOptions o;
  o.outputDir = m_dir->text();
  o.pathMode = m_pathMode->currentData().toInt();
  o.overwriteMode = m_overwrite->currentData().toInt();
  o.password = m_password->text();
  return o;
}
