#pragma once

#include <QString>

class QApplication;
class QWidget;

namespace Theme {
void apply(QApplication &app);
QString styleSheet();
void stripModalIcons(QWidget *widget);
}
