#include "PropVariantQt.h"
#include "Unicode.h"

#include "Windows/PropVariantConv.h"
#include "Windows/TimeUtils.h"
#include <QTimeZone>
#include <cwchar>

QString fileTimeToString(const FILETIME &ft)
{
  wchar_t buf[64];
  if (!ConvertUtcFileTimeToString(ft, buf, kTimestampPrintLevel_SEC))
    return QString();
  return u2q(buf, wcslen(buf));
}

QDateTime fileTimeToDateTime(const FILETIME &ft)
{
  const UInt64 v = FILETIME_To_UInt64(ft);
  if (v == 0)
    return QDateTime();
  const qint64 msecs = qint64(v / 10000ULL) - 11644473600000LL;
  QDateTime dt = QDateTime::fromMSecsSinceEpoch(msecs, QTimeZone(QByteArrayLiteral("UTC")));
  return dt.toLocalTime();
}

quint64 propToUInt64(const PROPVARIANT &prop, bool *ok)
{
  UInt64 value = 0;
  try
  {
    const bool res = ConvertPropVariantToUInt64(prop, value);
    if (ok) *ok = res;
    return quint64(value);
  }
  catch (...)
  {
    if (ok) *ok = false;
    return 0;
  }
}

bool propToBool(const PROPVARIANT &prop, bool *ok)
{
  if (ok) *ok = true;
  switch (prop.vt)
  {
    case VT_BOOL: return prop.boolVal != VARIANT_FALSE;
    case VT_UI1: return prop.bVal != 0;
    case VT_UI4: return prop.ulVal != 0;
    case VT_UI8: return prop.uhVal.QuadPart != 0;
    case VT_EMPTY: break;
    default: break;
  }
  if (ok) *ok = false;
  return false;
}

QString propToString(const PROPVARIANT &prop)
{
  switch (prop.vt)
  {
    case VT_EMPTY:
    case VT_NULL:
      return QString();
    case VT_BSTR:
      return u2q(prop.bstrVal, SysStringLen(prop.bstrVal));
    case VT_FILETIME:
      return fileTimeToString(prop.filetime);
    case VT_BOOL:
      return prop.boolVal != VARIANT_FALSE ? QStringLiteral("Yes") : QStringLiteral("No");
    default:
    {
      wchar_t buf[64];
      ConvertPropVariantToShortString(prop, buf);
      return u2q(buf, wcslen(buf));
    }
  }
}
