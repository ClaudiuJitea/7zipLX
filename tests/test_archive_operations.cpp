#include <QtTest/QtTest>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QQueue>
#include <QTemporaryDir>

#include "backend/ArchiveSession.h"
#include "backend/ArchiveWriteBack.h"
#include "backend/CompressOperation.h"
#include "backend/HashOperation.h"
#include "backend/LocalFileOperation.h"
#include "backend/PromptDispatcher.h"
#include "backend/SevenZipEngine.h"

class FakePromptDispatcher final : public PromptDispatcher
{
public:
  QString password;
  QQueue<OverwriteAnswer> overwriteAnswers;
  int passwordCalls = 0;
  int overwriteCalls = 0;
  int errorCalls = 0;
  QStringList errors;

  bool askPassword(QString *passwordOut, bool *ok, const QString &) override
  {
    ++passwordCalls;
    if (password.isEmpty())
    {
      if (ok)
        *ok = false;
      return false;
    }
    if (passwordOut)
      *passwordOut = password;
    if (ok)
      *ok = true;
    return true;
  }

  OverwriteAnswer askOverwrite(const OverwriteInfo &) override
  {
    ++overwriteCalls;
    if (overwriteAnswers.isEmpty())
      return OverwriteAnswer::Cancel;
    return overwriteAnswers.dequeue();
  }

  void showError(const QString &, const QString &text) override
  {
    ++errorCalls;
    errors << text;
  }
};

static bool writeFile(const QString &path, const QByteArray &contents)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  return file.write(contents) == contents.size();
}

static QByteArray readFile(const QString &path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return file.readAll();
}

static QString formatNameForExtension(const QString &extension)
{
  if (extension == QLatin1String("gz"))
    return QStringLiteral("gzip");
  return extension;
}

class ArchiveOperationsTest : public QObject
{
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void createAndExtract();
  void passwordHandling();
  void overwriteConflict();
  void cancelOnOverwrite();
  void nestedWriteBack();
  void singleFileCompressorRebuild();
  void pathTraversalIsContained();
  void streamedArchiveHash();
  void localCopyAndMove();

private:
  QString makeArchive(const QString &name, const QStringList &sourcePaths,
                      const QString &format, const QString &password = QString(),
                      bool encryptHeaders = false);
  QString extractOne(ArchiveSession &session, int index, QTemporaryDir &dir,
                     const QString &password = QString());
  QString testDir() const;

  QTemporaryDir m_root;
  FakePromptDispatcher m_prompts;
};

QString ArchiveOperationsTest::testDir() const
{
  static int counter = 0;
  const QString dir = m_root.path() + QStringLiteral("/t%1").arg(++counter);
  QDir().mkpath(dir);
  return dir;
}

void ArchiveOperationsTest::initTestCase()
{
  QVERIFY2(m_root.isValid(), "Cannot create the test root");
  QString error;
  QVERIFY2(SevenZipEngine::instance().initialize(&error), qPrintable(error));
}

void ArchiveOperationsTest::cleanupTestCase()
{
  SevenZipEngine::instance().shutdown();
}

QString ArchiveOperationsTest::makeArchive(const QString &name, const QStringList &sourcePaths,
                                           const QString &format, const QString &password,
                                           bool encryptHeaders)
{
  const QString archivePath = m_root.path() + QLatin1Char('/') + name;
  if (QFileInfo::exists(archivePath))
    QFile::remove(archivePath);
  CompressOptions options;
  options.archivePath = archivePath;
  options.formatIndex = SevenZipEngine::instance().formatIndex(formatNameForExtension(format));
  options.password = password;
  options.encryptHeaders = encryptHeaders;
  options.level = 1;
  const OperationResult result = compressFiles(sourcePaths, options, nullptr, &m_prompts);
  if (!result.ok)
    qWarning("Cannot create %s: %s", qPrintable(archivePath), qPrintable(result.error));
  return result.ok ? archivePath : QString();
}

QString ArchiveOperationsTest::extractOne(ArchiveSession &session, int index, QTemporaryDir &dir,
                                          const QString &password)
{
  ExtractOptions options;
  options.outputDir = dir.path();
  options.pathMode = 2;
  options.overwriteMode = 1;
  options.password = password.isEmpty() ? session.password() : password;
  const OperationResult result = session.extract({index}, options, nullptr, &m_prompts);
  if (!result.ok)
    return {};
  QDirIterator it(dir.path(), QDir::Files, QDirIterator::Subdirectories);
  return it.hasNext() ? it.next() : QString();
}

void ArchiveOperationsTest::createAndExtract()
{
  for (const QString &format : {QStringLiteral("7z"), QStringLiteral("zip")})
  {
    const QString source = testDir();
    QVERIFY(writeFile(source + QStringLiteral("/alpha.txt"), "alpha"));
    QVERIFY(writeFile(source + QStringLiteral("/nested/beta.txt"), "beta"));

    const QString archive = makeArchive(QStringLiteral("roundtrip.") + format, {source}, format);
    QVERIFY2(!archive.isEmpty(), qPrintable(format));

    ArchiveSession session;
    QString error;
    QVERIFY2(session.open(archive, QString(), nullptr, &m_prompts, &error), qPrintable(error));
    QVERIFY(session.itemCount() >= 1);

    const QString output = testDir();
    ExtractOptions options;
    options.outputDir = output;
    options.pathMode = 0;
    options.overwriteMode = 1;
    options.password = session.password();
    const OperationResult result = session.extract({}, options, nullptr, &m_prompts);
    QVERIFY2(result.ok, qPrintable(result.error));
    QDirIterator extracted(output, QDir::Files, QDirIterator::Subdirectories);
    QString alphaPath;
    QString betaPath;
    while (extracted.hasNext())
    {
      const QString path = extracted.next();
      if (path.endsWith(QStringLiteral("/alpha.txt")))
        alphaPath = path;
      else if (path.endsWith(QStringLiteral("/nested/beta.txt")))
        betaPath = path;
    }
    QVERIFY(!alphaPath.isEmpty());
    QVERIFY(!betaPath.isEmpty());
    QCOMPARE(readFile(alphaPath), QByteArray("alpha"));
    QCOMPARE(readFile(betaPath), QByteArray("beta"));
    session.close();
  }
}

void ArchiveOperationsTest::passwordHandling()
{
  const QString source = testDir();
  QVERIFY(writeFile(source + QStringLiteral("/secret.txt"), "classified"));
  const QString archive = makeArchive(QStringLiteral("secret.7z"), {source},
                                      QStringLiteral("7z"), QStringLiteral("s3cret"), true);
  QVERIFY(!archive.isEmpty());

  ArchiveSession wrong;
  QString error;
  QVERIFY(!wrong.open(archive, QString(), nullptr, &m_prompts, &error, QStringLiteral("wrong")));

  ArchiveSession prompted;
  m_prompts.password = QStringLiteral("s3cret");
  const int before = m_prompts.passwordCalls;
  QVERIFY2(prompted.open(archive, QString(), nullptr, &m_prompts, &error), qPrintable(error));
  QVERIFY(m_prompts.passwordCalls > before);

  QTemporaryDir out;
  const QString file = extractOne(prompted, 0, out);
  QVERIFY(!file.isEmpty());
  QCOMPARE(readFile(file), QByteArray("classified"));

  QStringList lines;
  const OperationResult hashed = prompted.hashItems({}, {QStringLiteral("SHA256")}, &lines,
                                                    nullptr, &m_prompts);
  QVERIFY2(hashed.ok, qPrintable(hashed.error));
  QVERIFY(!lines.isEmpty());
}

void ArchiveOperationsTest::overwriteConflict()
{
  const QString source = testDir();
  QVERIFY(writeFile(source + QStringLiteral("/file.txt"), "archive"));
  const QString archive = makeArchive(QStringLiteral("overwrite.zip"), {source + QStringLiteral("/file.txt")},
                                      QStringLiteral("zip"));
  QVERIFY(!archive.isEmpty());

  ArchiveSession session;
  QString error;
  QVERIFY2(session.open(archive, QString(), nullptr, &m_prompts, &error), qPrintable(error));

  const QString output = testDir();
  QVERIFY(writeFile(output + QStringLiteral("/file.txt"), "local"));

  ExtractOptions options;
  options.outputDir = output;
  options.pathMode = 2;
  options.overwriteMode = 0;

  m_prompts.overwriteAnswers.enqueue(OverwriteAnswer::No);
  OperationResult result = session.extract({0}, options, nullptr, &m_prompts);
  QVERIFY2(result.ok, qPrintable(result.error));
  QCOMPARE(m_prompts.overwriteCalls, 1);
  QCOMPARE(readFile(output + QStringLiteral("/file.txt")), QByteArray("local"));

  m_prompts.overwriteAnswers.enqueue(OverwriteAnswer::Yes);
  result = session.extract({0}, options, nullptr, &m_prompts);
  QVERIFY2(result.ok, qPrintable(result.error));
  QCOMPARE(m_prompts.overwriteCalls, 2);
  QCOMPARE(readFile(output + QStringLiteral("/file.txt")), QByteArray("archive"));
}

void ArchiveOperationsTest::cancelOnOverwrite()
{
  const QString source = testDir();
  QVERIFY(writeFile(source + QStringLiteral("/cancel.txt"), "archive"));
  const QString archive = makeArchive(QStringLiteral("cancel.zip"),
                                      {source + QStringLiteral("/cancel.txt")}, QStringLiteral("zip"));
  QVERIFY(!archive.isEmpty());

  ArchiveSession session;
  QString error;
  QVERIFY2(session.open(archive, QString(), nullptr, &m_prompts, &error), qPrintable(error));

  const QString output = testDir();
  QVERIFY(writeFile(output + QStringLiteral("/cancel.txt"), "keep me"));

  ExtractOptions options;
  options.outputDir = output;
  options.pathMode = 2;
  options.overwriteMode = 0;
  m_prompts.overwriteAnswers.enqueue(OverwriteAnswer::Cancel);
  const OperationResult result = session.extract({0}, options, nullptr, &m_prompts);
  QVERIFY(result.canceled || !result.ok);
  QCOMPARE(readFile(output + QStringLiteral("/cancel.txt")), QByteArray("keep me"));
}

void ArchiveOperationsTest::nestedWriteBack()
{
  // inner.zip contains old.txt = "old"; outer.7z contains inner.zip.
  const QString innerSource = testDir();
  QVERIFY(writeFile(innerSource + QStringLiteral("/old.txt"), "old"));
  const QString innerArchive = makeArchive(QStringLiteral("inner.zip"),
                                           {innerSource + QStringLiteral("/old.txt")},
                                           QStringLiteral("zip"));
  QVERIFY(!innerArchive.isEmpty());

  const QString outerArchive = makeArchive(QStringLiteral("outer.7z"), {innerArchive},
                                           QStringLiteral("7z"));
  QVERIFY(!outerArchive.isEmpty());

  // Pull the nested archive out, replace its payload, then put it back.
  ArchiveSession outer;
  QString error;
  QVERIFY2(outer.open(outerArchive, QString(), nullptr, &m_prompts, &error), qPrintable(error));
  QVERIFY(outer.itemCount() >= 1);
  QTemporaryDir outerOut;
  const QString nestedFile = extractOne(outer, 0, outerOut);
  QVERIFY(!nestedFile.isEmpty());
  outer.close();

  const QString replacementDir = testDir();
  QVERIFY(writeFile(replacementDir + QStringLiteral("/old.txt"), "new"));
  OperationResult replaced = replaceArchiveEntry(nestedFile, QString(), 
                                                 replacementDir + QStringLiteral("/old.txt"),
                                                 QStringLiteral("zip"), QString(), nullptr, &m_prompts);
  QVERIFY2(replaced.ok, qPrintable(replaced.error));

  replaced = replaceArchiveEntry(outerArchive, QString(), nestedFile, QStringLiteral("7z"),
                                 QString(), nullptr, &m_prompts);
  QVERIFY2(replaced.ok, qPrintable(replaced.error));

  // Verify the write-back through the public archive API.
  ArchiveSession checkOuter;
  QVERIFY2(checkOuter.open(outerArchive, QString(), nullptr, &m_prompts, &error), qPrintable(error));
  QTemporaryDir checkOut;
  const QString checkNested = extractOne(checkOuter, 0, checkOut);
  QVERIFY(!checkNested.isEmpty());
  checkOuter.close();

  ArchiveSession checkInner;
  QVERIFY2(checkInner.open(checkNested, QString(), nullptr, &m_prompts, &error), qPrintable(error));
  QTemporaryDir checkInnerOut;
  const QString payload = extractOne(checkInner, 0, checkInnerOut);
  QVERIFY(!payload.isEmpty());
  QCOMPARE(readFile(payload), QByteArray("new"));
}

void ArchiveOperationsTest::singleFileCompressorRebuild()
{
  const QString source = testDir();
  QVERIFY(writeFile(source + QStringLiteral("/payload.txt"), "old payload"));
  const QString archive = makeArchive(QStringLiteral("payload.txt.gz"),
                                      {source + QStringLiteral("/payload.txt")},
                                      QStringLiteral("gz"));
  QVERIFY(!archive.isEmpty());

  const QString replacementDir = testDir();
  QVERIFY(writeFile(replacementDir + QStringLiteral("/payload.txt"), "new payload"));
  const OperationResult result = replaceArchiveEntry(
      archive, QString(), replacementDir + QStringLiteral("/payload.txt"), QStringLiteral("gzip"),
      QString(), nullptr, &m_prompts);
  QVERIFY2(result.ok, qPrintable(result.error));
  QVERIFY(QFileInfo(archive).size() > 0);

  ArchiveSession session;
  QString error;
  QVERIFY2(session.open(archive, QString(), nullptr, &m_prompts, &error), qPrintable(error));
  QTemporaryDir out;
  const QString payload = extractOne(session, 0, out);
  QVERIFY(!payload.isEmpty());
  QCOMPARE(readFile(payload), QByteArray("new payload"));
}

namespace {

void tarWriteOctal(char *field, size_t size, quint64 value)
{
  const QByteArray digits = QByteArray::number(value, 8).rightJustified(int(size - 1), '0');
  memcpy(field, digits.constData(), size - 1);
  field[size - 1] = '\0';
}

void tarWriteEntry(QByteArray *out, const QByteArray &name, const QByteArray &linkName,
                   char type, const QByteArray &contents)
{
  QByteArray header(512, '\0');
  memcpy(header.data(), name.constData(), qMin<qsizetype>(name.size(), 100));
  tarWriteOctal(header.data() + 100, 8, 0644);
  tarWriteOctal(header.data() + 108, 8, 0);
  tarWriteOctal(header.data() + 116, 8, 0);
  tarWriteOctal(header.data() + 124, 12, quint64(contents.size()));
  tarWriteOctal(header.data() + 136, 12, 0);
  header[156] = type;
  memcpy(header.data() + 157, linkName.constData(), qMin<qsizetype>(linkName.size(), 100));
  memcpy(header.data() + 257, "ustar", 5);
  memcpy(header.data() + 263, "00", 2);
  memset(header.data() + 148, ' ', 8);
  unsigned sum = 0;
  for (char c : header)
    sum += static_cast<unsigned char>(c);
  const QByteArray checksum = QByteArray::number(sum, 8).rightJustified(6, '0') + '\0';
  memcpy(header.data() + 148, checksum.constData(), 7);
  out->append(header);
  out->append(contents);
  const int pad = (512 - (contents.size() % 512)) % 512;
  out->append(QByteArray(pad, '\0'));
}

} // namespace

void ArchiveOperationsTest::pathTraversalIsContained()
{
  const QString base = testDir();
  const QString tarPath = base + QStringLiteral("/traversal.tar");
  const QString absoluteTarget =
      QDir::tempPath() + QStringLiteral("/7ziplx-path-traversal-%1").arg(QCoreApplication::applicationPid());
  QFile::remove(absoluteTarget);

  QByteArray tar;
  tarWriteEntry(&tar, "../../escape.txt", {}, '0', "escaped");
  tarWriteEntry(&tar, QFile::encodeName(absoluteTarget), {}, '0', "absolute");
  tarWriteEntry(&tar, "link-to-passwd", "/etc/passwd", '2', {});
  tar.append(QByteArray(1024, '\0'));
  QVERIFY(writeFile(tarPath, tar));

  const QString output = base + QStringLiteral("/out");
  QDir().mkpath(output);
  ArchiveSession session;
  QString error;
  QVERIFY2(session.open(tarPath, QStringLiteral("tar"), nullptr, &m_prompts, &error), qPrintable(error));
  ExtractOptions options;
  options.outputDir = output;
  options.pathMode = 0;
  options.overwriteMode = 1;
  const OperationResult result = session.extract({}, options, nullptr, &m_prompts);
  QVERIFY2(result.ok, qPrintable(result.error));

  // Nothing may be written outside the output directory.
  QVERIFY(!QFileInfo::exists(base + QStringLiteral("/escape.txt")));
  QVERIFY(!QFileInfo::exists(absoluteTarget));
  const QString canonicalOut = QFileInfo(output).canonicalFilePath();
  QDirIterator it(output, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System | QDir::Hidden,
                  QDirIterator::Subdirectories);
  while (it.hasNext())
  {
    const QString path = it.next();
    const QFileInfo info(path);
    if (info.isSymLink())
    {
      const QString target = info.symLinkTarget();
      const QString resolved = QFileInfo(target).isAbsolute()
          ? target
          : info.absolutePath() + QLatin1Char('/') + target;
      const QString lexical = QDir::cleanPath(QFileInfo(resolved).absoluteFilePath());
      QVERIFY2(lexical.startsWith(canonicalOut + QLatin1Char('/')),
               qPrintable(QStringLiteral("symlink escapes output: %1 -> %2").arg(path, target)));
      const QString canonical = QFileInfo(resolved).canonicalFilePath();
      QVERIFY2(canonical.isEmpty() || canonical.startsWith(canonicalOut + QLatin1Char('/')),
               qPrintable(QStringLiteral("symlink target escapes output: %1 -> %2").arg(path, target)));
    }
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty())
      QVERIFY2(canonical.startsWith(canonicalOut + QLatin1Char('/')),
               qPrintable(QStringLiteral("entry escapes output: %1").arg(path)));
  }
  QFile::remove(absoluteTarget);
}

void ArchiveOperationsTest::streamedArchiveHash()
{
  const QString source = testDir();
  QVERIFY(writeFile(source + QStringLiteral("/one.txt"), "hello"));
  QVERIFY(writeFile(source + QStringLiteral("/two.txt"), "world"));
  const QString archive = makeArchive(QStringLiteral("hash.7z"),
                                      {source + QStringLiteral("/one.txt"),
                                       source + QStringLiteral("/two.txt")},
                                      QStringLiteral("7z"));
  QVERIFY(!archive.isEmpty());

  ArchiveSession session;
  QString error;
  QVERIFY2(session.open(archive, QString(), nullptr, &m_prompts, &error), qPrintable(error));
  QStringList streamedLines;
  const OperationResult streamed = session.hashItems({}, {QStringLiteral("SHA256")},
                                                     &streamedLines, nullptr, &m_prompts);
  QVERIFY2(streamed.ok, qPrintable(streamed.error));
  QVERIFY(!streamedLines.isEmpty());

  QTemporaryDir out;
  ExtractOptions options;
  options.outputDir = out.path();
  options.pathMode = 0;
  options.overwriteMode = 1;
  QVERIFY2(session.extract({}, options, nullptr, &m_prompts).ok, "extract for reference hash");
  session.close();

  QStringList files;
  QDirIterator it(out.path(), QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext())
    files << it.next();
  HashOptions hashOptions;
  hashOptions.methods << QStringLiteral("SHA256");
  const HashResult reference = hashFiles(files, hashOptions, nullptr, &m_prompts);
  QVERIFY2(reference.operation.ok, qPrintable(reference.operation.error));

  auto digestLine = [](const QStringList &lines) {
    for (const QString &line : lines)
      if (line.startsWith(QStringLiteral("SHA256: ")))
        return line;
    return QString();
  };
  QCOMPARE(digestLine(streamedLines), digestLine(reference.lines));
}

void ArchiveOperationsTest::localCopyAndMove()
{
  const QString source = testDir();
  QVERIFY(writeFile(source + QStringLiteral("/one.txt"), "one"));
  QVERIFY(writeFile(source + QStringLiteral("/nested/two.txt"), "two"));

  const QString copied = testDir();
  const OperationResult copy = copyLocalItems({source}, copied, false, nullptr, &m_prompts);
  QVERIFY2(copy.ok, qPrintable(copy.error));
  QVERIFY(QFileInfo::exists(copied + QStringLiteral("/") + QFileInfo(source).fileName() +
                            QStringLiteral("/one.txt")));
  QVERIFY(QFileInfo::exists(copied + QStringLiteral("/") + QFileInfo(source).fileName() +
                            QStringLiteral("/nested/two.txt")));

  const QString moved = testDir();
  const OperationResult move = copyLocalItems({source}, moved, true, nullptr, &m_prompts);
  QVERIFY2(move.ok, qPrintable(move.error));
  QVERIFY(!QFileInfo::exists(source));
  QVERIFY(QFileInfo::exists(moved + QStringLiteral("/") + QFileInfo(source).fileName() +
                            QStringLiteral("/one.txt")));
}

QTEST_MAIN(ArchiveOperationsTest)
#include "test_archive_operations.moc"
