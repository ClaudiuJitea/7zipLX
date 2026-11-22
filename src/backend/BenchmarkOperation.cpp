#include "BenchmarkOperation.h"
#include "ProgressState.h"
#include "PromptDispatcher.h"
#include "Unicode.h"
#include "SevenZipEngine.h"

#include <QLocale>

#include "Common/MyCom.h"
#include "Windows/PropVariant.h"
#include "7zip/UI/Common/Bench.h"
#include "7zip/UI/Common/Property.h"

#include <cstdio>

namespace {

QString hrText(HRESULT hr)
{
  return QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

QString speedText(const CBenchInfo &info)
{
  const UInt64 speed = info.GetUnpackSizeSpeed();
  return QStringLiteral("%1/s").arg(QLocale().formattedDataSize(qint64(speed), 1, QLocale::DataSizeTraditionalFormat));
}

class PrintCallback final : public IBenchPrintCallback
{
public:
  explicit PrintCallback(ProgressState *progress) : m_progress(progress) {}
  void Print(const char *s) override
  {
    if (!s) return;
    m_text += QString::fromUtf8(s);
    if (m_text.size() > 200000)
      m_text = m_text.right(100000);
  }
  void NewLine() override { m_text += QLatin1Char('\n'); }
  HRESULT CheckBreak() override { return (m_progress && m_progress->isCanceled()) ? E_ABORT : S_OK; }
  QStringList lines() const { return m_text.split(QLatin1Char('\n'), Qt::SkipEmptyParts); }
private:
  ProgressState *m_progress = nullptr;
  QString m_text;
};

class BenchCallback final : public IBenchCallback
{
public:
  explicit BenchCallback(ProgressState *progress) : m_progress(progress) {}
  HRESULT SetEncodeResult(const CBenchInfo &info, bool final) override
  {
    if (final)
    {
      const double ratio = info.PackSize ? double(info.Get_UnpackSize_Full()) / double(info.PackSize) : 0.0;
      m_lines << QStringLiteral("Compression: %1, %2 iterations, ratio %3")
                     .arg(speedText(info)).arg(info.NumIterations)
                     .arg(ratio > 0.0 ? QString::number(ratio, 'f', 2) : QStringLiteral("-"));
    }
    return check();
  }
  HRESULT SetDecodeResult(const CBenchInfo &info, bool final) override
  {
    if (final) m_lines << QStringLiteral("Decompression: %1, %2 iterations").arg(speedText(info)).arg(info.NumIterations);
    return check();
  }
  QStringList lines() const { return m_lines; }
private:
  HRESULT check() const { return (m_progress && m_progress->isCanceled()) ? E_ABORT : S_OK; }
  ProgressState *m_progress = nullptr;
  QStringList m_lines;
};

} // namespace

BenchmarkResult runBenchmark(const BenchmarkOptions &options, ProgressState *progress, PromptDispatcher *)
{
  BenchmarkResult result;
  QString engineError;
  if (!SevenZipEngine::instance().initialize(&engineError))
  {
    result.operation.error = engineError;
    return result;
  }

  CObjectVector<CProperty> props;
  auto add = [&props](const char *name, const UString &value) {
    CProperty p;
    p.Name = name;
    p.Value = value;
    props.Add(p);
  };
  add("m", q2u(options.method));
  UString dict;
  dict.Add_UInt64(options.dictionary >> 20);
  dict.Add_Char('m');
  add("d", dict);
  UString mt;
  mt.Add_UInt32(UInt32(qMax(1, options.threads)));
  add("mt", mt);
  if (options.timeMs > 0)
  {
    UString time;
    time.Add_UInt32(UInt32(options.timeMs));
    add("timems", time);
  }

  if (progress)
  {
    progress->setTitle(QStringLiteral("Benchmark"));
    progress->setStatus(QStringLiteral("Running benchmark..."));
  }

  PrintCallback print(progress);
  BenchCallback bench(progress);
  HRESULT res = E_FAIL;
  try
  {
#ifdef Z7_EXTERNAL_CODECS
    res = Bench(SevenZipEngine::instance().externalCodecs(), &print, &bench, props, UInt32(qMax(1, options.iterations)), false, nullptr);
#else
    res = Bench(&print, &bench, props, UInt32(qMax(1, options.iterations)), false, nullptr);
#endif
  }
  catch (const UString &s)
  {
    result.operation.error = u2q(s);
  }
  catch (const std::exception &e)
  {
    result.operation.error = QString::fromUtf8(e.what());
  }
  catch (...)
  {
    result.operation.error = QStringLiteral("Unknown benchmark error");
  }

  result.log = print.lines();
  result.log += bench.lines();
  result.operation.details = result.log;
  if (res == E_ABORT)
  {
    result.operation.canceled = true;
    result.operation.error = QStringLiteral("Benchmark canceled");
  }
  else if (res != S_OK)
  {
    if (result.operation.error.isEmpty())
      result.operation.error = QStringLiteral("Benchmark failed (%1)").arg(hrText(res));
  }
  else
  {
    result.operation.ok = true;
  }
  if (progress)
    progress->setStatus(result.operation.ok ? QStringLiteral("Finished")
                                            : (result.operation.canceled ? QStringLiteral("Canceled") : QStringLiteral("Failed")));
  return result;
}
