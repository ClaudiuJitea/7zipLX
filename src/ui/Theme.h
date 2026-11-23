#pragma once

#include <QIcon>
#include <QString>

class QApplication;
class QWidget;

namespace Theme {
enum class Mode
{
  System,
  Light,
  Dark
};

Mode modeFromString(const QString &value);
QString modeToString(Mode mode);

void apply(QApplication &app, Mode mode = Mode::System);
QString styleSheet(bool dark = true);
QIcon toolbarIcon(const QString &name);
void stripModalIcons(QWidget *widget);
}
