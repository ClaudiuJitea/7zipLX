#pragma once

#include <QDialog>
#include <QDialogButtonBox>
#include <QString>

class QLabel;
class QPushButton;
class QAbstractButton;

// Small themed message dialog used by prompts and UI error paths.  QMessageBox
// reserves an icon column even when it has no icon and does not expose the
// heading/body structure used by the rest of the application's dialogs.
class MessageDialog : public QDialog
{
  Q_OBJECT
public:
  enum class Kind
  {
    Information,
    Warning,
    Error,
    Question
  };

  explicit MessageDialog(QWidget *parent = nullptr);

  void setKind(Kind kind);
  void setHeading(const QString &heading);
  void setText(const QString &text, bool richText = false);
  void setInformativeText(const QString &text);

  QPushButton *addButton(const QString &text, QDialogButtonBox::ButtonRole role);
  void setDefaultButton(QPushButton *button);
  QPushButton *clickedButton() const { return m_clicked; }

  static void show(QWidget *parent, const QString &title, const QString &heading,
                   const QString &text, Kind kind = Kind::Information);
  static bool confirm(QWidget *parent, const QString &title, const QString &heading,
                      const QString &text, bool dangerous = false);

private:
  QLabel *m_heading = nullptr;
  QLabel *m_text = nullptr;
  QLabel *m_detail = nullptr;
  QDialogButtonBox *m_buttons = nullptr;
  QPushButton *m_clicked = nullptr;
};
