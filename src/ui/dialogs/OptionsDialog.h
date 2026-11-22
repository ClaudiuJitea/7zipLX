#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;

class OptionsDialog : public QDialog
{
  Q_OBJECT
public:
  OptionsDialog(bool showHidden, bool showDots, bool showGrid, bool flatView,
                const QString &viewer, const QString &editor, const QString &diff,
                const QString &language, QWidget *parent = nullptr);
  bool showHidden() const;
  bool showDots() const;
  bool showGrid() const;
  bool flatView() const;
  QString viewerCommand() const;
  QString editorCommand() const;
  QString diffCommand() const;
  QString language() const;

private slots:
  void setDefaultAssociation();
  void browseTranslations();

private:
  QCheckBox *m_hidden = nullptr;
  QCheckBox *m_dots = nullptr;
  QCheckBox *m_grid = nullptr;
  QCheckBox *m_flat = nullptr;
  QLineEdit *m_viewer = nullptr;
  QLineEdit *m_editor = nullptr;
  QLineEdit *m_diff = nullptr;
  QComboBox *m_language = nullptr;
};
