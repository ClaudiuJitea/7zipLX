#include "CompressDialog.h"
#include "DialogLayout.h"
#include "backend/SevenZipEngine.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QIcon>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

CompressDialog::CompressDialog(const QStringList &files, const QString &defaultDir, QWidget *parent)
    : QDialog(parent), m_files(files)
{
  setWindowTitle(CompressDialog::tr("Add to Archive"));
  setWindowIcon(QIcon());
  setMinimumWidth(580);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 22, 24, 18);
  layout->setSpacing(12);
  auto *heading = new QLabel(CompressDialog::tr("Add to Archive"), this);
  heading->setObjectName(QStringLiteral("dialogHeading"));
  layout->addWidget(heading);
  auto *sub = new QLabel(CompressDialog::tr("Create a new archive or update an existing one."), this);
  sub->setObjectName(QStringLiteral("dialogSubheading"));
  layout->addWidget(sub);
  auto *info = new QLabel(CompressDialog::tr("%1 item(s) selected").arg(files.size()), this);
  info->setObjectName(QStringLiteral("dialogInfo"));
  info->setWordWrap(true);
  layout->addWidget(info);

  auto *form = new QFormLayout();
  configureDialogForm(form);
  auto *pathRow = new QHBoxLayout();
  pathRow->setContentsMargins(0, 0, 0, 0);
  pathRow->setSpacing(8);
  m_archivePath = new QLineEdit(this);
  auto *browseButton = new QPushButton(CompressDialog::tr("Browse..."), this);
  pathRow->addWidget(m_archivePath, 1);
  pathRow->addWidget(browseButton);
  form->addRow(CompressDialog::tr("Archive:"), pathRow);

  m_format = new QComboBox(this);
  const auto formats = SevenZipEngine::instance().updateFormats();
  for (const QString &name : formats)
  {
    const int idx = SevenZipEngine::instance().formatIndex(name);
    const QString ext = SevenZipEngine::instance().formatExtension(idx);
    m_format->addItem(ext.isEmpty() ? name : CompressDialog::tr("%1 (*.%2)").arg(name, ext), idx);
  }
  const int def = SevenZipEngine::instance().formatIndex(QStringLiteral("7z"));
  if (def >= 0)
  {
    const int pos = m_format->findData(def);
    if (pos >= 0) m_format->setCurrentIndex(pos);
  }
  form->addRow(CompressDialog::tr("Format:"), m_format);

  m_level = new QComboBox(this);
  m_level->addItem(CompressDialog::tr("Store (0)"), 0);
  for (int i = 1; i <= 9; ++i)
    m_level->addItem(CompressDialog::tr("Level %1").arg(i), i);
  m_level->setCurrentIndex(5);
  form->addRow(CompressDialog::tr("Compression level:"), m_level);

  m_method = new QLineEdit(this);
  m_method->setPlaceholderText(QStringLiteral("format default"));
  form->addRow(CompressDialog::tr("Method:"), m_method);

  m_dictionary = new QLineEdit(this);
  m_dictionary->setPlaceholderText(CompressDialog::tr("e.g. 16m, 64m; empty = default"));
  form->addRow(CompressDialog::tr("Dictionary:"), m_dictionary);

  m_solid = new QCheckBox(CompressDialog::tr("Solid block"), this);
  form->addRow(QString(), m_solid);

  m_threads = new QSpinBox(this);
  m_threads->setRange(0, 256);
  m_threads->setSpecialValueText(CompressDialog::tr("Auto"));
  form->addRow(CompressDialog::tr("Threads:"), m_threads);

  m_volume = new QComboBox(this);
  m_volume->addItem(CompressDialog::tr("Single file"), quint64(0));
  m_volume->addItem(QStringLiteral("1 MiB"), quint64(1) << 20);
  m_volume->addItem(QStringLiteral("10 MiB"), quint64(10) << 20);
  m_volume->addItem(QStringLiteral("100 MiB"), quint64(100) << 20);
  m_volume->addItem(QStringLiteral("1 GiB"), quint64(1) << 30);
  form->addRow(CompressDialog::tr("Split volumes:"), m_volume);

  m_password = new QLineEdit(this);
  m_password->setEchoMode(QLineEdit::Password);
  form->addRow(CompressDialog::tr("Password:"), m_password);
  m_encryptHeaders = new QCheckBox(CompressDialog::tr("Encrypt file names (7z)"), this);
  form->addRow(QString(), m_encryptHeaders);

  layout->addLayout(form);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(browseButton, &QPushButton::clicked, this, &CompressDialog::browse);
  connect(m_format, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CompressDialog::formatChanged);

  QString base = QFileInfo(files.isEmpty() ? QStringLiteral("archive") : files.first()).completeBaseName();
  if (base.isEmpty()) base = QStringLiteral("archive");
  const QString dir = defaultDir.isEmpty() ? QDir::currentPath() : defaultDir;
  m_archivePath->setText(dir + QLatin1Char('/') + base + QStringLiteral(".7z"));
  formatChanged();
}

void CompressDialog::browse()
{
  const QString path = QFileDialog::getSaveFileName(this, CompressDialog::tr("Save archive"), m_archivePath->text());
  if (!path.isEmpty())
  {
    m_archivePath->setText(path);
    formatChanged();
  }
}

void CompressDialog::formatChanged()
{
  const int idx = m_format->currentData().toInt();
  const QString ext = SevenZipEngine::instance().formatExtension(idx);
  if (ext.isEmpty()) return;
  QString path = m_archivePath->text();
  if (path.isEmpty()) return;
  if (path.endsWith(QLatin1Char('.') + ext, Qt::CaseInsensitive))
  {
    m_lastExtension = ext;
    return;
  }
  if (!m_lastExtension.isEmpty() && path.endsWith(QLatin1Char('.') + m_lastExtension, Qt::CaseInsensitive))
    path.chop(m_lastExtension.size() + 1);
  path += QLatin1Char('.') + ext;
  m_lastExtension = ext;
  m_archivePath->setText(path);
}

CompressOptions CompressDialog::options() const
{
  CompressOptions o;
  o.archivePath = m_archivePath->text();
  o.formatIndex = m_format->currentData().toInt();
  o.level = m_level->currentData().toInt();
  o.method = m_method->text().trimmed();
  const QString dict = m_dictionary->text().trimmed();
  if (!dict.isEmpty())
  {
    QString num = dict;
    quint64 mult = 1;
    if (num.endsWith(QLatin1Char('k'), Qt::CaseInsensitive)) { mult = 1024ULL; num.chop(1); }
    else if (num.endsWith(QLatin1Char('m'), Qt::CaseInsensitive)) { mult = 1024ULL * 1024ULL; num.chop(1); }
    else if (num.endsWith(QLatin1Char('g'), Qt::CaseInsensitive)) { mult = 1024ULL * 1024ULL * 1024ULL; num.chop(1); }
    bool ok = false;
    const quint64 v = num.toULongLong(&ok);
    if (ok) o.dictionary = v * mult;
  }
  o.solid = m_solid->isChecked();
  o.numThreads = m_threads->value() == 0 ? -1 : m_threads->value();
  o.volumeSize = m_volume->currentData().toULongLong();
  o.password = m_password->text();
  o.encryptHeaders = m_encryptHeaders->isChecked();
  return o;
}
