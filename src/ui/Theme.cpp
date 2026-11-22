#include "Theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QMessageBox>
#include <QPalette>
#include <QStyleFactory>
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

void apply(QApplication &app)
{
  initAppResources();
  app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
  app.setFont(applicationFont());
  app.setPalette(darkPalette());
  app.setStyleSheet(styleSheet());
  app.installEventFilter(new NoModalIconsFilter(&app));
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

QString styleSheet()
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
}
QLabel#dialogHeading {
  font-size: 16pt;
  font-weight: 600;
  color: #f1f3f5;
  padding-bottom: 4px;
}
QLabel#dialogSubheading {
  color: #a4aab3;
  padding-bottom: 8px;
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
  padding: 8px 28px 8px 12px;
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
QToolButton:checked {
  background: #2c4f66;
  border-color: #3d6b88;
}
QToolButton:disabled {
  color: #6c7078;
}
QComboBox {
  background: #232428;
  border: 1px solid #35383f;
  border-radius: 8px;
  padding: 4px 9px;
  min-height: 28px;
}
QComboBox:hover {
  border-color: #4a4e57;
}
QComboBox:focus {
  border-color: #4f8cff;
}
QComboBox::drop-down {
  border: none;
  width: 22px;
}
QComboBox::down-arrow {
  image: url(:/icons/chevron-down.svg);
  width: 12px;
  height: 12px;
  margin-right: 7px;
}
QComboBox QAbstractItemView {
  background: #232428;
  border: 1px solid #34363d;
  border-radius: 8px;
  selection-background-color: #2c4f66;
  padding: 4px;
}
QLineEdit, QSpinBox {
  background: #232428;
  border: 1px solid #35383f;
  border-radius: 8px;
  padding: 5px 8px;
  min-height: 28px;
}
QLineEdit:hover, QSpinBox:hover {
  border-color: #4a4e57;
}
QLineEdit:focus, QSpinBox:focus {
  border-color: #4f8cff;
}
QPlainTextEdit, QTextEdit {
  background: #202126;
  border: 1px solid #35383f;
  border-radius: 8px;
  padding: 6px;
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
QTableView::item:selected {
  background: #2b4b63;
  color: #ffffff;
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
}
QStatusBar::item {
  border: none;
}
QPushButton {
  background: #2a2c31;
  border: 1px solid #3a3d44;
  border-radius: 8px;
  padding: 6px 14px;
  min-height: 28px;
}
QPushButton:hover {
  background: #32353b;
  border-color: #4a4e57;
}
QPushButton:pressed {
  background: #26282d;
}
QPushButton:default {
  background: #2e6da4;
  border-color: #4f8cff;
  color: #ffffff;
}
QPushButton:default:hover {
  background: #357bb8;
}
QPushButton:disabled {
  color: #6c7078;
  border-color: #303239;
  background: #242529;
}
QDialogButtonBox QPushButton {
  min-width: 84px;
}
QCheckBox {
  spacing: 8px;
  background: transparent;
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
  spacing: 8px;
  background: transparent;
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
  border: 1px solid #34363d;
  border-radius: 8px;
  margin-top: 14px;
  padding-top: 10px;
  background: #1e1f22;
}
QGroupBox::title {
  subcontrol-origin: margin;
  left: 12px;
  padding: 0 5px;
  color: #aab0b9;
}
QProgressBar {
  background: #232428;
  border: 1px solid #34363d;
  border-radius: 6px;
  text-align: center;
  min-height: 16px;
}
QProgressBar::chunk {
  background: #2e6da4;
  border-radius: 5px;
}
QTabWidget::pane {
  border: 1px solid #34363d;
  border-radius: 8px;
  top: -1px;
}
QTabBar::tab {
  background: transparent;
  padding: 7px 14px;
  border-radius: 6px;
  color: #aab0b9;
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
  background: #1e1f22;
}
QMessageBox QLabel {
  background: transparent;
}
QFileDialog QTreeView, QFileDialog QListView {
  background: #1e1f22;
  border: 1px solid #2c2e34;
}
)QSS");
}

} // namespace Theme
