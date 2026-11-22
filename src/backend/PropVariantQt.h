#pragma once

#include <QString>
#include <QDateTime>
#include <QtGlobal>

#include "Windows/PropVariant.h"

QString propToString(const PROPVARIANT &prop);
QString fileTimeToString(const FILETIME &ft);
QDateTime fileTimeToDateTime(const FILETIME &ft);
quint64 propToUInt64(const PROPVARIANT &prop, bool *ok = nullptr);
bool propToBool(const PROPVARIANT &prop, bool *ok = nullptr);
