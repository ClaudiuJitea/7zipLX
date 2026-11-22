#pragma once

#include <QString>
#include <QByteArray>

#include "Common/MyString.h"

// 7-Zip uses wchar_t (UTF-32 on Linux) for UString and UTF-8 for FString.
QString u2q(const wchar_t *s, size_t len);
QString u2q(const UString &s);
UString q2u(const QString &s);

QString fs2q(CFSTR s);
FString q2fs(const QString &s);
