#include "ShellCommand.h"

QStringList splitShellCommand(const QString &command, bool *ok)
{
  QStringList words;
  QString current;
  bool inWord = false;
  enum { Plain, Single, Double } state = Plain;
  for (int i = 0; i < command.size(); ++i)
  {
    const QChar c = command.at(i);
    switch (state)
    {
      case Plain:
        if (c.isSpace())
        {
          if (inWord)
          {
            words << current;
            current.clear();
            inWord = false;
          }
        }
        else if (c == QLatin1Char('\''))
        {
          state = Single;
          inWord = true;
        }
        else if (c == QLatin1Char('"'))
        {
          state = Double;
          inWord = true;
        }
        else if (c == QLatin1Char('\\') && i + 1 < command.size())
        {
          current += command.at(++i);
          inWord = true;
        }
        else
        {
          current += c;
          inWord = true;
        }
        break;
      case Single:
        if (c == QLatin1Char('\''))
          state = Plain;
        else
          current += c;
        break;
      case Double:
        if (c == QLatin1Char('"'))
          state = Plain;
        else if (c == QLatin1Char('\\') && i + 1 < command.size() &&
                 QStringLiteral("\"\\$`").contains(command.at(i + 1)))
          current += command.at(++i);
        else
          current += c;
        break;
    }
  }
  if (state != Plain)
  {
    if (ok)
      *ok = false;
    return {};
  }
  if (inWord)
    words << current;
  if (ok)
    *ok = true;
  return words;
}
