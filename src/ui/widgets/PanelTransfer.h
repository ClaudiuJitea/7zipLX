#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <Qt>

// MIME type used for drags and clipboard content that originate inside the
// application.  It carries enough information to let the receiving panel pick
// the right operation (copy/move/extract/add) without going through temporary
// files.
inline constexpr char kPanelTransferMime[] = "application/x-7ziplx-panel-items";
inline constexpr char kLegacyCutMime[] = "application/x-7ziplx-cut";

struct PanelTransfer
{
  enum Kind
  {
    None = 0,
    Local = 1,
    Archive = 2,
    External = 3,
  };

  Kind kind = None;
  int sourcePanelId = -1;
  QString sourceRoot;        // local directory or archive file path
  QString archivePrefix;     // folder inside the archive when it was captured
  QStringList localPaths;    // absolute local paths
  QVector<int> archiveRows;  // rows in the source archive's current folder
  QStringList archivePaths;  // archive-internal paths (used for diagnostics/reopening)
  QStringList names;         // base names of the selected items
  bool cut = false;

  bool isValid() const;
  int count() const;

  QByteArray encode() const;
  static PanelTransfer decode(const QByteArray &data);
  static PanelTransfer fromUrls(const QStringList &paths, bool cut);
};

struct PanelDropTarget
{
  bool archive = false;
  QString localDir;
  QString archivePrefix;

  bool isValid() const;
};

// Reads the cut/copy state that the common Linux file managers attach to a
// clipboard selection (or our own custom payload).
bool panelTransferIsCut(const class QMimeData *mime);
