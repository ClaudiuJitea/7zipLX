#include "ArchiveTypes.h"

#include <QFileInfo>
#include <QRegularExpression>

const QStringList &archiveExtensions()
{
  static const QStringList exts = {
      QStringLiteral("7z"),     QStringLiteral("zip"),   QStringLiteral("zipx"),
      QStringLiteral("rar"),    QStringLiteral("r00"),   QStringLiteral("tar"),
      QStringLiteral("gz"),     QStringLiteral("gzip"),  QStringLiteral("tgz"),
      QStringLiteral("tpz"),    QStringLiteral("bz2"),   QStringLiteral("bzip2"),
      QStringLiteral("tbz"),    QStringLiteral("tbz2"),  QStringLiteral("xz"),
      QStringLiteral("txz"),    QStringLiteral("zst"),   QStringLiteral("tzst"),
      QStringLiteral("lzma"),   QStringLiteral("lzma86"), QStringLiteral("z"),
      QStringLiteral("taz"),    QStringLiteral("cab"),   QStringLiteral("iso"),
      QStringLiteral("udf"),    QStringLiteral("wim"),   QStringLiteral("swm"),
      QStringLiteral("esd"),    QStringLiteral("arj"),   QStringLiteral("lzh"),
      QStringLiteral("lha"),    QStringLiteral("cpio"),  QStringLiteral("rpm"),
      QStringLiteral("deb"),    QStringLiteral("udeb"),  QStringLiteral("ar"),
      QStringLiteral("jar"),    QStringLiteral("xpi"),   QStringLiteral("apk"),
      QStringLiteral("xar"),    QStringLiteral("pkg"),   QStringLiteral("dmg"),
      QStringLiteral("squashfs"), QStringLiteral("cramfs"), QStringLiteral("ova"),
      QStringLiteral("001"),    QStringLiteral("z01"),
  };
  return exts;
}

bool isArchiveFileName(const QString &fileName)
{
  const QString suffix = QFileInfo(fileName).suffix().toLower();
  return !suffix.isEmpty() && archiveExtensions().contains(suffix);
}

QString archiveFolderName(const QString &archivePath)
{
  QString name = QFileInfo(archivePath).fileName();
  // Split volumes first: "x.7z.001", "x.zip.001", "x.part1.rar".
  static const QRegularExpression volume(QStringLiteral("(\\.part\\d+)?\\.(\\d{3}|r\\d\\d|z\\d\\d)$"),
                                         QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression partRar(QStringLiteral("\\.part\\d+(?=\\.rar$)"),
                                          QRegularExpression::CaseInsensitiveOption);
  name.remove(partRar);
  name.remove(volume);
  // Then every trailing archive extension: "x.tar.gz" -> "x".
  for (int pass = 0; pass < 4; ++pass)
  {
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0)
      break;
    const QString ext = name.mid(dot + 1).toLower();
    if (!archiveExtensions().contains(ext))
      break;
    name.truncate(dot);
  }
  if (name.isEmpty() || name == QLatin1String("."))
    name = QStringLiteral("extracted");
  return name;
}
