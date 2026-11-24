#pragma once

#include <QString>
#include <QStringList>

// Per-user file manager integration that a package cannot install system-wide:
// GNOME's desktop (Desktop Icons NG) and Nautilus only read scripts from
// ~/.local/share/nautilus/scripts, so 7zipLX keeps a "7zipLX" submenu there.
// Nautilus windows additionally get a context-aware menu from the
// nautilus-python extension installed by the package.
namespace DesktopIntegration {

struct Script
{
  QString fileName; // shown as the menu entry
  QString arguments; // passed to 7ziplx before the selected paths
};

const QVector<Script> &fileManagerScripts();
QString scriptsDirectory(); // ~/.local/share/nautilus/scripts/7zipLX
QString scriptContents(const Script &script, const QString &program);

// Installs or refreshes the scripts (enabled) or removes them (disabled).
// Only files carrying the 7zipLX marker are touched.  Returns false and sets
// error when the directory cannot be written.
bool syncFileManagerScripts(bool enabled, QString *error = nullptr,
                            const QString &program = QStringLiteral("7ziplx"));

} // namespace DesktopIntegration
