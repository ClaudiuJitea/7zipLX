#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class FilePanel;
class OperationRunner;
class PromptDispatcher;
class QWidget;

// Hash actions for local files and archive items.  Archive items are hashed by
// streaming the decoded data through the 7-Zip hashers; no temporary files are
// written.
class HashController : public QObject
{
  Q_OBJECT
public:
  HashController(QWidget *parent, OperationRunner *runner, PromptDispatcher *prompts);

  void promptAndHash(FilePanel *panel);
  void hashSelection(FilePanel *panel, const QString &method);

private:
  void showResult(const QString &method, const QString &title, const QStringList &lines);

  QWidget *m_parent = nullptr;
  OperationRunner *m_runner = nullptr;
  PromptDispatcher *m_prompts = nullptr;
};
