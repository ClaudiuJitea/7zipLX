#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QVBoxLayout;

// Themed replacement for QInputDialog.  It uses the same form geometry as the
// application's larger dialogs (right-aligned labels, matching control heights)
// instead of QInputDialog's platform-dependent internal layout.
class InputDialog : public QDialog
{
  Q_OBJECT
public:
  explicit InputDialog(QWidget *parent = nullptr);

  void setHeading(const QString &heading);
  void setSubheading(const QString &subheading);

  QLineEdit *addLineEdit(const QString &label, const QString &value, bool password = false);
  QPlainTextEdit *addPlainTextEdit(const QString &label, const QString &value);
  QComboBox *addComboBox(const QString &label, const QStringList &items, int current = 0);
  QSpinBox *addSpinBox(const QString &label, int minimum, int maximum, int value);

  static bool getText(QWidget *parent, const QString &title, const QString &heading,
                      const QString &label, QString *value, bool password = false);
  static bool getMultiLineText(QWidget *parent, const QString &title, const QString &heading,
                               const QString &label, QString *value);
  static bool getItem(QWidget *parent, const QString &title, const QString &heading,
                      const QString &label, const QStringList &items, int current,
                      QString *value);
  static bool getInt(QWidget *parent, const QString &title, const QString &heading,
                     const QString &label, int minimum, int maximum, int value,
                     int *result);

private:
  QVBoxLayout *m_layout = nullptr;
  QLabel *m_heading = nullptr;
  QLabel *m_subheading = nullptr;
  QFormLayout *m_form = nullptr;
  QDialogButtonBox *m_buttons = nullptr;
};
