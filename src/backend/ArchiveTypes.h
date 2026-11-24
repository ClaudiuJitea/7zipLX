#pragma once

#include <QString>
#include <QStringList>

// File types the UI treats as archives: double-click opens them in 7zipLX and
// the extract/test actions apply.  7-Zip can open more (executables, office
// documents, disk images), but those are reachable through "Open Inside".
bool isArchiveFileName(const QString &fileName);

// Every extension in the list above, lower case, without the dot.
const QStringList &archiveExtensions();

// Folder name used by "Extract to <name>/": the archive name without its
// archive extensions ("photos.tar.gz" -> "photos", "disk.7z.001" -> "disk").
QString archiveFolderName(const QString &archivePath);
