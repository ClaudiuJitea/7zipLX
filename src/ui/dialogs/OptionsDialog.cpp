#include "OptionsDialog.h"
#include "DialogLayout.h"
#include "backend/MessageDialog.h"

#include <QApplication>
#include <QCoreApplication>
#include "backend/SevenZipEngine.h"
#include <QCheckBox>
#include <QIcon>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

OptionsDialog::OptionsDialog(bool showHidden, bool showDots, bool showGrid, bool flatView,
                             const QString &viewer, const QString &editor, const QString &diff,
                             const QString &language, QWidget *parent)
    : QDialog(parent)
{
  setWindowTitle(QStringLiteral("Options"));
  setWindowIcon(QIcon());
  setMinimumSize(560, 430);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 22, 24, 18);
  layout->setSpacing(12);
  auto *heading = new QLabel(QStringLiteral("Options"), this);
  heading->setObjectName(QStringLiteral("dialogHeading"));
  layout->addWidget(heading);
  auto *sub = new QLabel(QStringLiteral("Configure the file list, external tools, associations and language."), this);
  sub->setObjectName(QStringLiteral("dialogSubheading"));
  sub->setWordWrap(true);
  layout->addWidget(sub);

  auto *tabs = new QTabWidget(this);
  layout->addWidget(tabs, 1);

  auto *general = new QWidget(tabs);
  auto *generalLayout = new QVBoxLayout(general);
  generalLayout->setContentsMargins(14, 14, 14, 14);
  auto *listGroup = new QGroupBox(QStringLiteral("File list"), general);
  auto *listForm = new QVBoxLayout(listGroup);
  listForm->setContentsMargins(14, 18, 14, 14);
  listForm->setSpacing(8);
  m_hidden = new QCheckBox(QStringLiteral("Show hidden files"), listGroup);
  m_hidden->setChecked(showHidden);
  listForm->addWidget(m_hidden);
  m_dots = new QCheckBox(QStringLiteral("Show parent entry (..)"), listGroup);
  m_dots->setChecked(showDots);
  listForm->addWidget(m_dots);
  m_grid = new QCheckBox(QStringLiteral("Show grid lines"), listGroup);
  m_grid->setChecked(showGrid);
  listForm->addWidget(m_grid);
  m_flat = new QCheckBox(QStringLiteral("Flat view (show nested items)"), listGroup);
  m_flat->setChecked(flatView);
  listForm->addWidget(m_flat);
  generalLayout->addWidget(listGroup);
  generalLayout->addStretch();
  tabs->addTab(general, QStringLiteral("General"));

  auto *tools = new QWidget(tabs);
  auto *toolsForm = new QFormLayout(tools);
  configureDialogForm(toolsForm);
  toolsForm->setContentsMargins(14, 14, 14, 14);
  m_viewer = new QLineEdit(viewer, tools);
  m_viewer->setPlaceholderText(QStringLiteral("xdg-open"));
  toolsForm->addRow(QStringLiteral("Viewer command:"), m_viewer);
  m_editor = new QLineEdit(editor, tools);
  m_editor->setPlaceholderText(QStringLiteral("xdg-open"));
  toolsForm->addRow(QStringLiteral("Editor command:"), m_editor);
  m_diff = new QLineEdit(diff, tools);
  m_diff->setPlaceholderText(QStringLiteral("diff -u"));
  toolsForm->addRow(QStringLiteral("Diff command:"), m_diff);
  auto *toolsNote = new QLabel(QStringLiteral("Commands are split into arguments and receive the selected file path(s). Use an absolute path or a command on PATH."), tools);
  toolsNote->setWordWrap(true);
  toolsNote->setObjectName(QStringLiteral("dialogSubheading"));
  toolsForm->addRow(toolsNote);
  tabs->addTab(tools, QStringLiteral("Tools"));

  auto *plugins = new QWidget(tabs);
  auto *pluginLayout = new QVBoxLayout(plugins);
  pluginLayout->setContentsMargins(14, 14, 14, 14);
  auto *pluginInfo = new QLabel(plugins);
  pluginInfo->setWordWrap(true);
  pluginInfo->setTextFormat(Qt::RichText);
  int moduleCount = 0;
#ifdef Z7_EXTERNAL_CODECS
  moduleCount = SevenZipEngine::instance().externalCodecModuleCount();
#endif
  const QString modulePath = QCoreApplication::applicationDirPath() + QStringLiteral("/7z.so");
  pluginInfo->setText(QStringLiteral(
      "<b>External 7z.so codec/format module</b><br><br>"
      "Path: <code>%1</code><br>"
      "Status: %2<br>"
      "Loaded modules: %3<br>"
      "Registered formats (de-duplicated): %4")
      .arg(modulePath.toHtmlEscaped(),
           moduleCount > 0 ? QStringLiteral("loaded") : QStringLiteral("not loaded (built-in formats remain available)"),
           QString::number(moduleCount),
           QString::number(SevenZipEngine::instance().archiveFormats().size())));
  pluginLayout->addWidget(pluginInfo);
  auto *pluginNote = new QLabel(QStringLiteral("The module is optional. Third-party modules must use the 7-Zip 26.06 ABI and export GetModuleProp."), plugins);
  pluginNote->setWordWrap(true);
  pluginNote->setObjectName(QStringLiteral("dialogSubheading"));
  pluginLayout->addWidget(pluginNote);
  pluginLayout->addStretch();
  tabs->addTab(plugins, QStringLiteral("Plugins"));

  auto *assoc = new QWidget(tabs);
  auto *assocLayout = new QVBoxLayout(assoc);
  assocLayout->setContentsMargins(14, 14, 14, 14);
  auto *assocNote = new QLabel(QStringLiteral("Register 7zipLX as a desktop handler for archive MIME types. The desktop file must already be installed."), assoc);
  assocNote->setWordWrap(true);
  assocLayout->addWidget(assocNote);
  auto *assocButton = new QPushButton(QStringLiteral("Set as default for 7z and ZIP"), assoc);
  assocLayout->addWidget(assocButton, 0, Qt::AlignLeft);
  assocLayout->addStretch();
  connect(assocButton, &QPushButton::clicked, this, &OptionsDialog::setDefaultAssociation);
  tabs->addTab(assoc, QStringLiteral("Associations"));

  auto *languagePage = new QWidget(tabs);
  auto *languageForm = new QFormLayout(languagePage);
  configureDialogForm(languageForm);
  languageForm->setContentsMargins(14, 14, 14, 14);
  m_language = new QComboBox(languagePage);
  m_language->addItem(QStringLiteral("System default"), QStringLiteral("system"));
  m_language->addItem(QStringLiteral("English"), QStringLiteral("en"));
  const QStringList translationDirs = {
      QApplication::applicationDirPath() + QStringLiteral("/translations"),
      QApplication::applicationDirPath() + QStringLiteral("/../share/7ziplx/translations"),
      QStringLiteral("/usr/share/7ziplx/translations")};
  for (const QString &dirPath : translationDirs)
  {
    const QDir dir(dirPath);
    for (const QString &file : dir.entryList({QStringLiteral("7ziplx_*.qm")}, QDir::Files))
    {
      const QString code = QFileInfo(file).completeBaseName().section(QLatin1Char('_'), 1);
      if (m_language->findData(code) < 0)
        m_language->addItem(code, code);
    }
  }
  const int langIndex = m_language->findData(language.isEmpty() ? QStringLiteral("system") : language);
  if (langIndex >= 0) m_language->setCurrentIndex(langIndex);
  languageForm->addRow(QStringLiteral("Language:"), m_language);
  auto *langNote = new QLabel(QStringLiteral("Language changes take effect after restarting 7zipLX. Translation files use the name 7ziplx_<code>.qm."), languagePage);
  langNote->setWordWrap(true);
  langNote->setObjectName(QStringLiteral("dialogSubheading"));
  languageForm->addRow(langNote);
  tabs->addTab(languagePage, QStringLiteral("Language"));

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool OptionsDialog::showHidden() const { return m_hidden->isChecked(); }
bool OptionsDialog::showDots() const { return m_dots->isChecked(); }
bool OptionsDialog::showGrid() const { return m_grid->isChecked(); }
bool OptionsDialog::flatView() const { return m_flat->isChecked(); }
QString OptionsDialog::viewerCommand() const { return m_viewer->text().trimmed(); }
QString OptionsDialog::editorCommand() const { return m_editor->text().trimmed(); }
QString OptionsDialog::diffCommand() const { return m_diff->text().trimmed(); }
QString OptionsDialog::language() const { return m_language->currentData().toString(); }

void OptionsDialog::setDefaultAssociation()
{
  const QString desktop = QStringLiteral("7ziplx.desktop");
  QStringList failed;
  const QStringList mimes = {
      QStringLiteral("application/x-7z-compressed"),
      QStringLiteral("application/zip"),
      QStringLiteral("application/x-tar"),
      QStringLiteral("application/gzip"),
      QStringLiteral("application/x-xz"),
      QStringLiteral("application/x-bzip2")};
  for (const QString &mime : mimes)
  {
    if (!QProcess::startDetached(QStringLiteral("xdg-mime"), {QStringLiteral("default"), desktop, mime}))
      failed << mime;
  }
  if (failed.isEmpty())
    MessageDialog::show(this, QStringLiteral("Associations"), QStringLiteral("Associations"),
                        QStringLiteral("Requested default handlers. The desktop file must be installed for this to take effect."),
                        MessageDialog::Kind::Information);
  else
    MessageDialog::show(this, QStringLiteral("Associations"), QStringLiteral("Associations"),
                        QStringLiteral("Could not run xdg-mime for: %1").arg(failed.join(QStringLiteral(", "))),
                        MessageDialog::Kind::Warning);
}

void OptionsDialog::browseTranslations()
{
}
