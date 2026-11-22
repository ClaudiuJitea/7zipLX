#include "Unicode.h"

#include <QVector>
#include <cwchar>

QString u2q(const wchar_t *s, size_t len)
{
  if (!s)
    return QString();
#if WCHAR_MAX > 0xFFFF
  return QString::fromUcs4(reinterpret_cast<const char32_t *>(s), int(len));
#else
  return QString::fromUtf16(reinterpret_cast<const char16_t *>(s), int(len));
#endif
}

QString u2q(const UString &s)
{
  return u2q(s.Ptr(), s.Len());
}

UString q2u(const QString &s)
{
  const QVector<uint> ucs4 = s.toUcs4();
  UString out;
  wchar_t *buf = out.GetBuf_SetEnd((unsigned)ucs4.size());
  for (int i = 0; i < ucs4.size(); ++i)
    buf[i] = (wchar_t)ucs4[i];
  return out;
}

QString fs2q(CFSTR s)
{
  if (!s)
    return QString();
  return QString::fromUtf8(s);
}

FString q2fs(const QString &s)
{
  const QByteArray utf8 = s.toUtf8();
  return FString(utf8.constData());
}
