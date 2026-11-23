#include "PanelTransfer.h"

#include <QDataStream>
#include <QFileInfo>
#include <QIODevice>
#include <QMimeData>
#include <QSet>
#include <QUrl>

namespace {
constexpr quint32 kFormatVersion = 1;

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
constexpr QDataStream::Version kStreamVersion = QDataStream::Qt_6_5;
#else
constexpr QDataStream::Version kStreamVersion = QDataStream::Qt_6_4;
#endif

QString normalizedArchivePrefix(const QString &prefix)
{
  QString p = prefix;
  p.replace(QLatin1Char('\\'), QLatin1Char('/'));
  while (p.startsWith(QLatin1Char('/')))
    p.remove(0, 1);
  while (p.endsWith(QLatin1Char('/')))
    p.chop(1);
  return p;
}
} // namespace

bool PanelTransfer::isValid() const
{
  if (kind == Local || kind == External)
    return !localPaths.isEmpty();
  if (kind == Archive)
    return !sourceRoot.isEmpty() && !archiveRows.isEmpty();
  return false;
}

int PanelTransfer::count() const
{
  if (kind == Archive)
    return archiveRows.size();
  return localPaths.size();
}

QByteArray PanelTransfer::encode() const
{
  QByteArray data;
  QDataStream out(&data, QIODevice::WriteOnly);
  out.setVersion(kStreamVersion);
  out << kFormatVersion
      << quint8(kind)
      << qint32(sourcePanelId)
      << sourceRoot
      << archivePrefix
      << localPaths
      << QList<int>(archiveRows.begin(), archiveRows.end())
      << archivePaths
      << names
      << cut;
  return data;
}

PanelTransfer PanelTransfer::decode(const QByteArray &data)
{
  PanelTransfer transfer;
  if (data.isEmpty())
    return transfer;
  QDataStream in(data);
  in.setVersion(kStreamVersion);
  quint32 version = 0;
  quint8 kind = 0;
  qint32 panelId = -1;
  QList<int> rows;
  in >> version >> kind >> panelId >> transfer.sourceRoot >> transfer.archivePrefix
     >> transfer.localPaths >> rows >> transfer.archivePaths >> transfer.names >> transfer.cut;
  if (!in.atEnd() || in.status() != QDataStream::Ok || version != kFormatVersion)
    return PanelTransfer();
  if (kind > quint8(External))
    return PanelTransfer();
  transfer.kind = Kind(kind);
  transfer.sourcePanelId = panelId;
  transfer.archiveRows = QVector<int>(rows.begin(), rows.end());
  transfer.archivePrefix = normalizedArchivePrefix(transfer.archivePrefix);
  return transfer;
}

PanelTransfer PanelTransfer::fromUrls(const QStringList &paths, bool cut)
{
  PanelTransfer transfer;
  transfer.kind = External;
  transfer.cut = cut;
  for (const QString &path : paths)
  {
    if (path.isEmpty())
      continue;
    if (!transfer.localPaths.contains(path))
    {
      transfer.localPaths.append(path);
      transfer.names << QFileInfo(path).fileName();
    }
  }
  return transfer;
}

bool PanelDropTarget::isValid() const
{
  return archive || !localDir.isEmpty();
}

bool panelTransferIsCut(const QMimeData *mime)
{
  if (!mime)
    return false;
  if (mime->hasFormat(QString::fromLatin1(kPanelTransferMime)))
  {
    const PanelTransfer transfer =
        PanelTransfer::decode(mime->data(QString::fromLatin1(kPanelTransferMime)));
    if (transfer.isValid())
      return transfer.cut;
  }
  if (mime->hasFormat(QStringLiteral("x-special/gnome-copied-files")))
  {
    const QByteArray data = mime->data(QStringLiteral("x-special/gnome-copied-files"));
    return data.startsWith("cut");
  }
  if (mime->hasFormat(QStringLiteral("application/x-kde-cutselection")))
    return mime->data(QStringLiteral("application/x-kde-cutselection")) == QByteArrayLiteral("1");
  if (mime->hasFormat(QString::fromLatin1(kLegacyCutMime)))
    return mime->data(QString::fromLatin1(kLegacyCutMime)) == QByteArrayLiteral("1");
  return false;
}
