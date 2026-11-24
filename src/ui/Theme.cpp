#include "Theme.h"
#include "dialogs/DialogLayout.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHash>
#include <QMessageBox>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTimer>
#include <QWidget>

namespace {

QFont applicationFont()
{
  const QStringList families = QFontDatabase::families();
  const QStringList preferred = {
      QStringLiteral("Inter"),
      QStringLiteral("Noto Sans"),
      QStringLiteral("Cantarell"),
      QStringLiteral("Source Sans 3"),
      QStringLiteral("DejaVu Sans"),
  };
  for (const QString &family : preferred)
    if (families.contains(family, Qt::CaseInsensitive))
      return QFont(family, 10);
  return QFont(QStringLiteral("Sans Serif"), 10);
}

QPalette darkPalette()
{
  QPalette p;
  p.setColor(QPalette::Window, QColor("#1e1f22"));
  p.setColor(QPalette::WindowText, QColor("#e6e7e9"));
  p.setColor(QPalette::Base, QColor("#1e1f22"));
  p.setColor(QPalette::AlternateBase, QColor("#222428"));
  p.setColor(QPalette::ToolTipBase, QColor("#2a2c31"));
  p.setColor(QPalette::ToolTipText, QColor("#e6e7e9"));
  p.setColor(QPalette::Text, QColor("#e6e7e9"));
  p.setColor(QPalette::Button, QColor("#2a2c31"));
  p.setColor(QPalette::ButtonText, QColor("#e6e7e9"));
  p.setColor(QPalette::BrightText, QColor("#ffffff"));
  p.setColor(QPalette::Link, QColor("#6fa8dc"));
  p.setColor(QPalette::Highlight, QColor("#2b4b63"));
  p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
  p.setColor(QPalette::PlaceholderText, QColor("#7d838c"));
  p.setColor(QPalette::Disabled, QPalette::Text, QColor("#737880"));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#737880"));
  p.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#737880"));
  p.setColor(QPalette::Disabled, QPalette::Base, QColor("#202126"));
  p.setColor(QPalette::Disabled, QPalette::Button, QColor("#242529"));
  return p;
}

class NoModalIconsFilter final : public QObject
{
public:
  explicit NoModalIconsFilter(QObject *parent) : QObject(parent) {}

  bool eventFilter(QObject *watched, QEvent *event) override
  {
    if (event->type() == QEvent::Show)
    {
      if (auto *widget = qobject_cast<QWidget *>(watched))
      {
        if (widget->isWindow() && (widget->windowType() == Qt::Dialog ||
                                   widget->windowType() == Qt::Sheet ||
                                   qobject_cast<QMessageBox *>(widget)))
        {
          strip(widget);
          for (QFormLayout *form : widget->findChildren<QFormLayout *>())
            alignDialogFormLabels(form);
          QTimer::singleShot(0, widget, [widget] { strip(widget); });
        }
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  static void strip(QWidget *root)
  {
    if (!root)
      return;
    root->setWindowIcon(QIcon());
    if (auto *box = qobject_cast<QMessageBox *>(root))
      box->setIcon(QMessageBox::NoIcon);
    for (QDialogButtonBox *buttons : root->findChildren<QDialogButtonBox *>())
      for (QAbstractButton *button : buttons->buttons())
        button->setIcon(QIcon());
  }
};

} // namespace

static void initAppResources()
{
  Q_INIT_RESOURCE(app);
}

namespace Theme {

static Mode g_mode = Mode::System;

static int g_systemDark = -1;

static bool systemIsDark()
{
  if (g_systemDark >= 0)
    return g_systemDark == 1;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  if (const QStyleHints *hints = QGuiApplication::styleHints())
  {
    const Qt::ColorScheme scheme = hints->colorScheme();
    if (scheme == Qt::ColorScheme::Dark)
    {
      g_systemDark = 1;
      return true;
    }
    if (scheme == Qt::ColorScheme::Light)
    {
      g_systemDark = 0;
      return false;
    }
  }
#endif
  const QPalette palette = QApplication::palette();
  g_systemDark = palette.color(QPalette::Window).lightness() < 128 ? 1 : 0;
  return g_systemDark == 1;
}

Mode modeFromString(const QString &value)
{
  const QString normalized = value.trimmed().toLower();
  if (normalized == QLatin1String("light"))
    return Mode::Light;
  if (normalized == QLatin1String("dark"))
    return Mode::Dark;
  return Mode::System;
}

QString modeToString(Mode mode)
{
  switch (mode)
  {
    case Mode::Light: return QStringLiteral("light");
    case Mode::Dark: return QStringLiteral("dark");
    default: return QStringLiteral("system");
  }
}

void apply(QApplication &app, Mode mode)
{
  initAppResources();
  g_mode = mode;
  const bool systemDark = systemIsDark();
  const bool dark = mode == Mode::Dark || (mode == Mode::System && systemDark);
  app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
  app.setFont(applicationFont());
  app.setPalette(dark ? darkPalette() : app.style()->standardPalette());
  app.setStyleSheet(styleSheet(dark));
  static bool watcherInstalled = false;
  if (!watcherInstalled)
  {
    watcherInstalled = true;
    auto *watcher = new QObject(&app);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, watcher,
                     [](Qt::ColorScheme) {
                       g_systemDark = -1;
                       if (g_mode == Mode::System && qApp)
                         apply(*qApp, Mode::System);
                     });
#endif
    Q_UNUSED(watcher);
  }
  static bool filterInstalled = false;
  if (!filterInstalled)
  {
    filterInstalled = true;
    app.installEventFilter(new NoModalIconsFilter(&app));
  }
}

void stripModalIcons(QWidget *widget)
{
  if (!widget)
    return;
  widget->setWindowIcon(QIcon());
  if (auto *box = qobject_cast<QMessageBox *>(widget))
    box->setIcon(QMessageBox::NoIcon);
  for (QDialogButtonBox *buttons : widget->findChildren<QDialogButtonBox *>())
    for (QAbstractButton *button : buttons->buttons())
      button->setIcon(QIcon());
}

static QString darkStyleSheet()
{
  return QStringLiteral(R"QSS(
QWidget {
  background: #1e1f22;
  color: #e6e7e9;
  selection-background-color: #2b4b63;
  selection-color: #ffffff;
}
QMainWindow, QDialog {
  background: #1b1c20;
}
#filePanel {
  background: #1e1f22;
  border: 1px solid #2c2e34;
  border-radius: 8px;
}
#filePanel[active="true"] {
  border: 1px solid #3d5a70;
}
#filePanel[dropActive="true"] {
  border: 1px solid #4f8cff;
  background: #202832;
}
#panelPathBar {
  background: #232428;
  border: 1px solid #35383f;
  border-radius: 8px;
  padding: 3px 8px;
  min-height: 22px;
  max-height: 22px;
}
QLabel#brand {
  color: #f1f3f5;
  font-weight: 700;
  font-size: 12pt;
  padding: 0 10px 0 4px;
  background: transparent;
}
QLabel {
  background: transparent;
  qproperty-indent: 0;
}
QLabel#dialogHeading {
  font-size: 15pt;
  font-weight: 600;
  color: #f1f3f5;
  padding-bottom: 2px;
}
QLabel#dialogSubheading {
  color: #969eaa;
  padding-bottom: 8px;
}
QLabel#dialogInfo {
  background: #202329;
  border: 1px solid #30343c;
  border-radius: 9px;
  padding: 8px 11px;
  color: #b7bfc9;
}
QLabel#messageHeading {
  font-size: 13pt;
  font-weight: 600;
  color: #f1f3f5;
  padding-bottom: 2px;
}
QLabel#messageText {
  color: #d9dde3;
}
QLabel#messageDetail {
  color: #9aa2ad;
  font-size: 9pt;
}
QLabel#progressStatus {
  color: #aab2bd;
}
QLabel#progressFile {
  color: #d9dde3;
}
QLabel#progressStats {
  color: #9aa2ad;
}
QLabel#panelStatus {
  background: #18191c;
  border-top: 1px solid #2c2e34;
  color: #9aa0a8;
  padding: 5px 9px;
}
QLabel#panelStatus[active="true"] {
  background: #1b232b;
  color: #cfe0ee;
}
QMenuBar {
  background: #18191c;
  border-bottom: 1px solid #2c2e34;
  padding: 2px 6px;
}
QMenuBar::item {
  padding: 7px 12px;
  margin: 1px;
  border-radius: 6px;
  background: transparent;
}
QMenuBar::item:selected {
  background: #2c4f66;
  color: #ffffff;
}
QMenu {
  background: #232428;
  border: 1px solid #34363d;
  border-radius: 10px;
  padding: 6px;
}
QMenu::item {
  padding: 6px 28px 6px 12px;
  border-radius: 6px;
  background: transparent;
}
QMenu::item:selected {
  background: #2c4f66;
  color: #ffffff;
}
QMenu::item:disabled {
  color: #6c7078;
}
QMenu::separator {
  height: 1px;
  background: #34363d;
  margin: 5px 10px;
}
QToolBar {
  background: #18191c;
  border: none;
  border-bottom: 1px solid #2c2e34;
  spacing: 4px;
  padding: 5px 8px;
}
QToolBar::separator {
  width: 1px;
  background: #34363d;
  margin: 4px 7px;
}
QToolButton {
  background: transparent;
  border: 1px solid transparent;
  border-radius: 7px;
  padding: 5px 8px;
  color: #d7d9dd;
}
QToolButton:hover {
  background: #292b30;
  border-color: #3a3d45;
}
QToolButton:pressed {
  background: #32353b;
}
QToolBar#mainToolbar QToolButton {
  padding: 5px 10px;
  min-height: 20px;
  max-height: 20px;
}
QWidget#toolbarSpacer {
  background: transparent;
}
QLabel#brandLogo {
  padding: 0 2px 0 4px;
}
QLineEdit#toolbarFilter {
  min-height: 26px;
  max-height: 26px;
  border-radius: 8px;
  padding: 0 8px;
  margin-right: 6px;
}
QToolButton:checked {
  background: #2c4f66;
  border-color: #3d6b88;
}
QToolButton:disabled {
  color: #6c7078;
}
QToolBar#mainToolbar QToolButton:checked {
  background: #32353b;
  border-color: #3a3d45;
}
QComboBox, QLineEdit, QSpinBox {
  background: #222429;
  border: 1px solid #3a3e46;
  border-radius: 9px;
  padding: 0 11px;
  min-height: 34px;
  color: #e6e7e9;
  selection-background-color: #2b4b63;
}
QComboBox {
  padding-right: 34px;
}
QSpinBox {
  padding-right: 38px;
  max-height: 34px;
}
QComboBox:hover, QLineEdit:hover, QSpinBox:hover {
  border-color: #4b505a;
}
QComboBox:focus, QLineEdit:focus, QSpinBox:focus {
  border-color: #4f8cff;
  background: #24262c;
}
QComboBox:disabled, QLineEdit:disabled, QSpinBox:disabled {
  color: #6f7580;
  background: #202126;
  border-color: #30333a;
}
QComboBox::drop-down {
  subcontrol-origin: border;
  subcontrol-position: top right;
  width: 30px;
  border-left: 1px solid #30333a;
  border-top-right-radius: 9px;
  border-bottom-right-radius: 9px;
  background: transparent;
}
QComboBox::drop-down:hover {
  background: #2b2e34;
}
QComboBox::down-arrow {
  image: url(:/icons/chevron-down.svg);
  width: 12px;
  height: 12px;
}
QComboBox QAbstractItemView {
  background: #23252a;
  border: 1px solid #3a3e46;
  border-radius: 9px;
  selection-background-color: #2c4f66;
  outline: 0;
  padding: 5px;
}
QComboBox QAbstractItemView::item {
  min-height: 26px;
  padding: 3px 8px;
  border-radius: 6px;
}
QSpinBox::up-button, QSpinBox::down-button {
  subcontrol-origin: border;
  width: 26px;
  border-left: 1px solid #30333a;
  background: transparent;
}
QSpinBox::up-button {
  subcontrol-position: top right;
  border-top-right-radius: 9px;
}
QSpinBox::down-button {
  subcontrol-position: bottom right;
  border-bottom-right-radius: 9px;
}
QSpinBox::up-button:hover, QSpinBox::down-button:hover {
  background: #2b2e34;
}
QSpinBox::up-arrow {
  image: url(:/icons/chevron-up.svg);
  width: 10px;
  height: 10px;
}
QSpinBox::down-arrow {
  image: url(:/icons/chevron-down.svg);
  width: 10px;
  height: 10px;
}
QPlainTextEdit, QTextEdit {
  background: #202126;
  border: 1px solid #343841;
  border-radius: 9px;
  padding: 8px;
  selection-background-color: #2b4b63;
}
QTableView {
  background: #1e1f22;
  alternate-background-color: #222428;
  border: none;
  selection-background-color: #2b4b63;
  selection-color: #ffffff;
  gridline-color: transparent;
}
QTableView::item {
  padding: 3px 6px;
  border: none;
}
QTableView::item:hover {
  background: #292b30;
}
QTableView::item:selected {
  background: #2b4b63;
  color: #ffffff;
}
QTableView::item:selected:hover {
  background: #2c4f66;
}
QHeaderView::section {
  font-weight: 600;
  background: #25272c;
  color: #aab0b9;
  border: none;
  border-right: 1px solid #30333a;
  border-bottom: 1px solid #30333a;
  padding: 7px 8px;
}
QHeaderView::section:hover {
  background: #2d3036;
  color: #e6e7e9;
}
QHeaderView::section:pressed {
  background: #2c4f66;
}
QScrollBar:vertical {
  background: transparent;
  width: 11px;
  margin: 2px;
}
QScrollBar::handle:vertical {
  background: #3b3e45;
  border-radius: 4px;
  min-height: 26px;
}
QScrollBar::handle:vertical:hover {
  background: #4b4f58;
}
QScrollBar:horizontal {
  background: transparent;
  height: 11px;
  margin: 2px;
}
QScrollBar::handle:horizontal {
  background: #3b3e45;
  border-radius: 4px;
  min-width: 26px;
}
QScrollBar::handle:horizontal:hover {
  background: #4b4f58;
}
QScrollBar::add-line, QScrollBar::sub-line {
  width: 0;
  height: 0;
  background: transparent;
}
QScrollBar::add-page, QScrollBar::sub-page {
  background: transparent;
}
QStatusBar {
  background: #18191c;
  border-top: 1px solid #2c2e34;
  color: #9aa0a8;
  padding: 0 8px;
  min-height: 24px;
}
QLabel#windowStatus {
  color: #9aa0a8;
  padding: 2px 4px;
}
QStatusBar::item {
  border: none;
}
QPushButton {
  background: #2a2d33;
  border: 1px solid #3b4049;
  border-radius: 9px;
  padding: 0 18px;
  min-height: 34px;
}
QPushButton:hover {
  background: #343840;
  border-color: #4b515c;
}
QPushButton:pressed {
  background: #24272c;
}
QPushButton:default {
  background: #2f6fc4;
  border-color: #4f8cff;
  color: #ffffff;
}
QPushButton:default:hover {
  background: #3a7bd0;
}
QPushButton:disabled {
  color: #6c7078;
  border-color: #303239;
  background: #242529;
}
QDialogButtonBox {
  border-top: 1px solid #2a2d33;
  padding-top: 12px;
}
QDialogButtonBox QPushButton {
  min-width: 92px;
}
QCheckBox {
  spacing: 9px;
  background: transparent;
  color: #cfd4dc;
  padding: 2px 0;
}
QCheckBox:disabled {
  color: #6f7580;
}
QCheckBox::indicator {
  width: 16px;
  height: 16px;
  border-radius: 4px;
  border: 1px solid #4a4e57;
  background: #232428;
}
QCheckBox::indicator:hover {
  border-color: #6b737e;
}
QCheckBox::indicator:checked {
  background: #4f8cff;
  border-color: #4f8cff;
  image: url(:/icons/check.svg);
}
QRadioButton {
  spacing: 9px;
  background: transparent;
  color: #cfd4dc;
  padding: 2px 0;
}
QRadioButton::indicator {
  width: 16px;
  height: 16px;
  border-radius: 8px;
  border: 1px solid #4a4e57;
  background: #232428;
}
QRadioButton::indicator:checked {
  background: #4f8cff;
  border: 4px solid #232428;
}
QGroupBox {
  border: 1px solid #30343c;
  border-radius: 10px;
  margin-top: 16px;
  padding: 12px;
  background: #1e2024;
}
QGroupBox::title {
  subcontrol-origin: margin;
  left: 12px;
  padding: 0 6px;
  color: #aab2bd;
  background: #1b1c20;
}
QProgressBar {
  background: #23252a;
  border: 1px solid #343841;
  border-radius: 8px;
  text-align: center;
  min-height: 18px;
  color: #dfe5ec;
}
QProgressBar::chunk {
  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #2f6fc4, stop:1 #4f8cff);
  border-radius: 7px;
}
QTabWidget::pane {
  border: 1px solid #30343c;
  border-radius: 10px;
  top: -1px;
  background: #1e2024;
}
QTabBar::tab {
  background: transparent;
  padding: 8px 16px;
  margin-right: 2px;
  border-radius: 8px;
  color: #9ba3ae;
}
QTabBar::tab:selected {
  background: #2c4f66;
  color: #ffffff;
}
QTabBar::tab:hover:!selected {
  background: #292b30;
}
QToolTip {
  background: #2a2c31;
  color: #e6e7e9;
  border: 1px solid #3a3d44;
  border-radius: 4px;
  padding: 4px 6px;
}
QSplitter::handle {
  background: #17181b;
}
QSplitter::handle:hover {
  background: #2e6da4;
}
QMessageBox {
  background: #1c1d21;
}
QMessageBox QLabel {
  background: transparent;
  color: #d9dde3;
}
QFileDialog QTreeView, QFileDialog QListView {
  background: #1e1f22;
  border: 1px solid #2c2e34;
}
)QSS");
}

static QString lightStyleSheet()
{
  // The dark sheet is the single source of truth for layout and component
  // structure.  Light mode only substitutes the palette colours and re-adds
  // the few rules whose text must stay white on the accent colour.
  static const QHash<QString, QString> colors = {
      {QStringLiteral("#17181b"), QStringLiteral("#dfe4ea")},
      {QStringLiteral("#18191c"), QStringLiteral("#eef1f5")},
      {QStringLiteral("#1b1c20"), QStringLiteral("#ffffff")},
      {QStringLiteral("#1b232b"), QStringLiteral("#e6f0fa")},
      {QStringLiteral("#1c1d21"), QStringLiteral("#ffffff")},
      {QStringLiteral("#1e1f22"), QStringLiteral("#f7f8fa")},
      {QStringLiteral("#1e2024"), QStringLiteral("#f3f5f8")},
      {QStringLiteral("#202126"), QStringLiteral("#eef1f5")},
      {QStringLiteral("#202329"), QStringLiteral("#f2f5f9")},
      {QStringLiteral("#202832"), QStringLiteral("#e8f1fb")},
      {QStringLiteral("#222428"), QStringLiteral("#f0f2f5")},
      {QStringLiteral("#222429"), QStringLiteral("#ffffff")},
      {QStringLiteral("#232428"), QStringLiteral("#ffffff")},
      {QStringLiteral("#23252a"), QStringLiteral("#ffffff")},
      {QStringLiteral("#242529"), QStringLiteral("#eceff3")},
      {QStringLiteral("#24262c"), QStringLiteral("#ffffff")},
      {QStringLiteral("#24272c"), QStringLiteral("#e4e8ee")},
      {QStringLiteral("#25272c"), QStringLiteral("#eef1f5")},
      {QStringLiteral("#292b30"), QStringLiteral("#e6ebf2")},
      {QStringLiteral("#2a2c31"), QStringLiteral("#e9ecf1")},
      {QStringLiteral("#2a2d33"), QStringLiteral("#f2f4f8")},
      {QStringLiteral("#2b2e34"), QStringLiteral("#eef2f7")},
      {QStringLiteral("#2b4b63"), QStringLiteral("#2f6fc4")},
      {QStringLiteral("#2c2e34"), QStringLiteral("#d8dde5")},
      {QStringLiteral("#2c4f66"), QStringLiteral("#2f6fc4")},
      {QStringLiteral("#2d3036"), QStringLiteral("#e3e8f0")},
      {QStringLiteral("#2e6da4"), QStringLiteral("#2f6fc4")},
      {QStringLiteral("#303239"), QStringLiteral("#d9dfe7")},
      {QStringLiteral("#30333a"), QStringLiteral("#d9dfe7")},
      {QStringLiteral("#30343c"), QStringLiteral("#d4dae3")},
      {QStringLiteral("#32353b"), QStringLiteral("#dfe5ed")},
      {QStringLiteral("#34363d"), QStringLiteral("#d4dae3")},
      {QStringLiteral("#343840"), QStringLiteral("#e6ebf2")},
      {QStringLiteral("#343841"), QStringLiteral("#d4dae3")},
      {QStringLiteral("#35383f"), QStringLiteral("#cfd6df")},
      {QStringLiteral("#3a3d44"), QStringLiteral("#c9d1dc")},
      {QStringLiteral("#3a3d45"), QStringLiteral("#c9d1dc")},
      {QStringLiteral("#3a3e46"), QStringLiteral("#c9d1dc")},
      {QStringLiteral("#3a7bd0"), QStringLiteral("#2560ad")},
      {QStringLiteral("#3b3e45"), QStringLiteral("#c3cbd6")},
      {QStringLiteral("#3b4049"), QStringLiteral("#c9d1dc")},
      {QStringLiteral("#3d5a70"), QStringLiteral("#7aa7cc")},
      {QStringLiteral("#3d6b88"), QStringLiteral("#7aa7cc")},
      {QStringLiteral("#4a4e57"), QStringLiteral("#9aa7b8")},
      {QStringLiteral("#4b4f58"), QStringLiteral("#aab4c2")},
      {QStringLiteral("#4b505a"), QStringLiteral("#9aa7b8")},
      {QStringLiteral("#4b515c"), QStringLiteral("#9aa7b8")},
      {QStringLiteral("#4f8cff"), QStringLiteral("#2f6fc4")},
      {QStringLiteral("#6b737e"), QStringLiteral("#6f7b8a")},
      {QStringLiteral("#6c7078"), QStringLiteral("#9aa1ab")},
      {QStringLiteral("#6f7580"), QStringLiteral("#9aa1ab")},
      {QStringLiteral("#6fa8dc"), QStringLiteral("#2f6fc4")},
      {QStringLiteral("#737880"), QStringLiteral("#9aa1ab")},
      {QStringLiteral("#7d838c"), QStringLiteral("#7a818c")},
      {QStringLiteral("#969eaa"), QStringLiteral("#5f6773")},
      {QStringLiteral("#9aa0a8"), QStringLiteral("#5f6773")},
      {QStringLiteral("#9aa2ad"), QStringLiteral("#5f6773")},
      {QStringLiteral("#9ba3ae"), QStringLiteral("#5f6773")},
      {QStringLiteral("#aab0b9"), QStringLiteral("#4a525d")},
      {QStringLiteral("#aab2bd"), QStringLiteral("#5f6773")},
      {QStringLiteral("#b7bfc9"), QStringLiteral("#3a424d")},
      {QStringLiteral("#cfd4dc"), QStringLiteral("#2f353d")},
      {QStringLiteral("#cfe0ee"), QStringLiteral("#24445e")},
      {QStringLiteral("#d7d9dd"), QStringLiteral("#2a3038")},
      {QStringLiteral("#d9dde3"), QStringLiteral("#2f353d")},
      {QStringLiteral("#dfe5ec"), QStringLiteral("#2f353d")},
      {QStringLiteral("#e6e7e9"), QStringLiteral("#20242a")},
      {QStringLiteral("#f1f3f5"), QStringLiteral("#11151a")},
  };
  QString css = darkStyleSheet();
  for (auto it = colors.constBegin(); it != colors.constEnd(); ++it)
    css.replace(it.key(), it.value());
  css += QStringLiteral(R"QSS(
QToolButton:checked, QMenuBar::item:selected, QMenu::item:selected,
QTableView::item:selected, QComboBox QAbstractItemView::item:selected,
QTabBar::tab:selected, QPushButton:default, QPushButton:default:hover {
  color: #ffffff;
}
QPushButton:default:hover { background: #2560ad; }
QToolBar#mainToolbar QToolButton:checked { color: #20242a; }
)QSS");
  return css;
}

QString styleSheet(bool dark)
{
  return dark ? darkStyleSheet() : lightStyleSheet();
}

QIcon toolbarIcon(const QString &name)
{
  const bool dark = g_mode == Mode::Dark || (g_mode == Mode::System && systemIsDark());
  const QString path = dark
      ? QStringLiteral(":/icons/toolbar/%1").arg(name)
      : QStringLiteral(":/icons/toolbar-light/%1").arg(name);
  return QIcon(path);
}

} // namespace Theme
