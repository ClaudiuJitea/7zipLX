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
                             const QString &language, const QString &theme, QWidget *parent)
    : QDialog(parent)
{
  setWindowTitle(OptionsDialog::tr("Options"));
  setWindowIcon(QIcon());
  setMinimumSize(560, 430);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 22, 24, 18);
  layout->setSpacing(12);
  auto *heading = new QLabel(OptionsDialog::tr("Options"), this);
  heading->setObjectName(QStringLiteral("dialogHeading"));
  layout->addWidget(heading);
  auto *sub = new QLabel(OptionsDialog::tr("Configure the file list, external tools, desktop integration and language."), this);
  sub->setObjectName(QStringLiteral("dialogSubheading"));
  sub->setWordWrap(true);
  layout->addWidget(sub);

  auto *tabs = new QTabWidget(this);
  layout->addWidget(tabs, 1);

  auto *general = new QWidget(tabs);
  auto *generalLayout = new QVBoxLayout(general);
  generalLayout->setContentsMargins(14, 14, 14, 14);
  auto *listGroup = new QGroupBox(OptionsDialog::tr("File list"), general);
  auto *listForm = new QVBoxLayout(listGroup);
  listForm->setContentsMargins(14, 18, 14, 14);
  listForm->setSpacing(8);
  m_hidden = new QCheckBox(OptionsDialog::tr("Show hidden files"), listGroup);
  m_hidden->setChecked(showHidden);
  listForm->addWidget(m_hidden);
  m_dots = new QCheckBox(OptionsDialog::tr("Show parent entry (..)"), listGroup);
  m_dots->setChecked(showDots);
  listForm->addWidget(m_dots);
  m_grid = new QCheckBox(OptionsDialog::tr("Show grid lines"), listGroup);
  m_grid->setChecked(showGrid);
  listForm->addWidget(m_grid);
  m_flat = new QCheckBox(OptionsDialog::tr("Flat view (show nested items)"), listGroup);
  m_flat->setChecked(flatView);
  listForm->addWidget(m_flat);
  generalLayout->addWidget(listGroup);

  auto *appearanceGroup = new QGroupBox(OptionsDialog::tr("Appearance"), general);
  auto *appearanceForm = new QFormLayout(appearanceGroup);
  appearanceForm->setContentsMargins(14, 18, 14, 14);
  m_theme = new QComboBox(appearanceGroup);
  m_theme->addItem(OptionsDialog::tr("Follow system"), QStringLiteral("system"));
  m_theme->addItem(OptionsDialog::tr("Light"), QStringLiteral("light"));
  m_theme->addItem(OptionsDialog::tr("Dark"), QStringLiteral("dark"));
  const int themeIndex = m_theme->findData(theme.isEmpty() ? QStringLiteral("system") : theme);
  if (themeIndex >= 0)
    m_theme->setCurrentIndex(themeIndex);
  appearanceForm->addRow(OptionsDialog::tr("Theme:"), m_theme);
  generalLayout->addWidget(appearanceGroup);
  generalLayout->addStretch();
  tabs->addTab(general, OptionsDialog::tr("General"));

  auto *tools = new QWidget(tabs);
  auto *toolsForm = new QFormLayout(tools);
  configureDialogForm(toolsForm);
  toolsForm->setContentsMargins(14, 14, 14, 14);
  m_viewer = new QLineEdit(viewer, tools);
  m_viewer->setPlaceholderText(QStringLiteral("xdg-open"));
  toolsForm->addRow(OptionsDialog::tr("Viewer command:"), m_viewer);
  m_editor = new QLineEdit(editor, tools);
  m_editor->setPlaceholderText(QStringLiteral("xdg-open"));
  toolsForm->addRow(OptionsDialog::tr("Editor command:"), m_editor);
  m_diff = new QLineEdit(diff, tools);
  m_diff->setPlaceholderText(QStringLiteral("diff -u"));
  toolsForm->addRow(OptionsDialog::tr("Diff command:"), m_diff);
  auto *toolsNote = new QLabel(OptionsDialog::tr("Commands are split into arguments and receive the selected file path(s). Use an absolute path or a command on PATH."), tools);
  toolsNote->setWordWrap(true);
  toolsNote->setObjectName(QStringLiteral("dialogSubheading"));
  toolsForm->addRow(toolsNote);
  tabs->addTab(tools, OptionsDialog::tr("Tools"));

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
  auto *pluginNote = new QLabel(OptionsDialog::tr("The module is optional. Third-party modules must use the 7-Zip 26.06 ABI and export GetModuleProp."), plugins);
  pluginNote->setWordWrap(true);
  pluginNote->setObjectName(QStringLiteral("dialogSubheading"));
  pluginLayout->addWidget(pluginNote);
  pluginLayout->addStretch();
  tabs->addTab(plugins, OptionsDialog::tr("Plugins"));

  auto *assoc = new QWidget(tabs);
  auto *assocLayout = new QVBoxLayout(assoc);
  assocLayout->setContentsMargins(14, 14, 14, 14);
  auto *menusGroup = new QGroupBox(OptionsDialog::tr("Right-click menus"), assoc);
  auto *menusLayout = new QVBoxLayout(menusGroup);
  menusLayout->setContentsMargins(14, 18, 14, 14);
  menusLayout->setSpacing(8);
  m_fileManagerMenus = new QCheckBox(OptionsDialog::tr("Show 7zipLX actions on the desktop and in the file manager"), menusGroup);
  m_fileManagerMenus->setChecked(true);
  menusLayout->addWidget(m_fileManagerMenus);
  auto *menusNote = new QLabel(OptionsDialog::tr(
      "Archives get Extract Here, Extract to Folder, Extract Files… and Test; other files get "
      "Add to Archive… and Compress to 7z / zip. On GNOME they appear under Scripts → 7zipLX; "
      "with python3-nautilus installed, Files windows also show a 7-Zip submenu."), menusGroup);
  menusNote->setWordWrap(true);
  menusNote->setObjectName(QStringLiteral("dialogSubheading"));
  menusLayout->addWidget(menusNote);
  assocLayout->addWidget(menusGroup);

  auto *defaultGroup = new QGroupBox(OptionsDialog::tr("Default application"), assoc);
  auto *defaultLayout = new QVBoxLayout(defaultGroup);
  defaultLayout->setContentsMargins(14, 18, 14, 14);
  defaultLayout->setSpacing(8);
  auto *assocNote = new QLabel(OptionsDialog::tr("Open archives with 7zipLX when they are double-clicked in the file manager."), defaultGroup);
  assocNote->setWordWrap(true);
  defaultLayout->addWidget(assocNote);
  auto *assocButton = new QPushButton(OptionsDialog::tr("Set as default for 7z and ZIP"), defaultGroup);
  defaultLayout->addWidget(assocButton, 0, Qt::AlignLeft);
  assocLayout->addWidget(defaultGroup);
  assocLayout->addStretch();
  connect(assocButton, &QPushButton::clicked, this, &OptionsDialog::setDefaultAssociation);
  tabs->addTab(assoc, OptionsDialog::tr("Integration"));

  auto *languagePage = new QWidget(tabs);
  auto *languageForm = new QFormLayout(languagePage);
  configureDialogForm(languageForm);
  languageForm->setContentsMargins(14, 14, 14, 14);
  m_language = new QComboBox(languagePage);
  m_language->addItem(OptionsDialog::tr("System default"), QStringLiteral("system"));
  m_language->addItem(OptionsDialog::tr("English"), QStringLiteral("en"));
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
  languageForm->addRow(OptionsDialog::tr("Language:"), m_language);
  auto *langNote = new QLabel(OptionsDialog::tr("Language changes take effect after restarting 7zipLX. Translation files use the name 7ziplx_<code>.qm."), languagePage);
  langNote->setWordWrap(true);
  langNote->setObjectName(QStringLiteral("dialogSubheading"));
  languageForm->addRow(langNote);
  tabs->addTab(languagePage, OptionsDialog::tr("Language"));

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
QString OptionsDialog::theme() const { return m_theme->currentData().toString(); }
bool OptionsDialog::fileManagerMenus() const { return m_fileManagerMenus->isChecked(); }
void OptionsDialog::setFileManagerMenus(bool enabled) { m_fileManagerMenus->setChecked(enabled); }

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
    MessageDialog::show(this, OptionsDialog::tr("Associations"), OptionsDialog::tr("Associations"),
                        OptionsDialog::tr("Requested default handlers. The desktop file must be installed for this to take effect."),
                        MessageDialog::Kind::Information);
  else
    MessageDialog::show(this, OptionsDialog::tr("Associations"), OptionsDialog::tr("Associations"),
                        OptionsDialog::tr("Could not run xdg-mime for: %1").arg(failed.join(QStringLiteral(", "))),
                        MessageDialog::Kind::Warning);
}

void OptionsDialog::browseTranslations()
{
}
