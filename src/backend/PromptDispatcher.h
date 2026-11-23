#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>

struct OverwriteInfo
{
  QString existingName;
  QString newName;
  QDateTime existingTime;
  QDateTime newTime;
  quint64 existingSize = 0;
  quint64 newSize = 0;
  bool existingSizeKnown = false;
  bool newSizeKnown = false;
};

enum class OverwriteAnswer
{
  Yes = 0,
  YesToAll = 1,
  No = 2,
  NoToAll = 3,
  AutoRename = 4,
  Cancel = 5
};

class PromptDispatcher : public QObject
{
  Q_OBJECT
public:
  explicit PromptDispatcher(QObject *parent = nullptr);

  // All methods are safe to call from a worker thread. They block that thread
  // while the prompt is executed on the GUI thread.
  virtual bool askPassword(QString *password, bool *ok, const QString &title = QString());
  virtual OverwriteAnswer askOverwrite(const OverwriteInfo &info);
  virtual bool confirm(const QString &title, const QString &text, bool dangerous = false);
  virtual void showError(const QString &title, const QString &text);
  virtual void showInfo(const QString &title, const QString &text);

private:
  template<typename Fn>
  void onMain(Fn &&fn) const;
};
