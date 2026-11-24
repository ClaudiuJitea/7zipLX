#pragma once

#include <QString>
#include <QStringList>

// Splits a command line the way a POSIX shell would for a simple command:
// whitespace separates words, 'single quotes' keep text literally, "double
// quotes" keep spaces and honour \" \\ \$ \`, and a backslash outside quotes
// escapes the next character.  No expansion is performed.  Returns an empty
// list (and sets *ok to false) for an unterminated quote.
QStringList splitShellCommand(const QString &command, bool *ok = nullptr);
