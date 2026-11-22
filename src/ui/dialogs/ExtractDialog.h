#pragma once

#include <QDialog>

#include "backend/ArchiveSession.h"

class QLineEdit;
class QComboBox;
class QCheckBox;

class ExtractDialog : public QDialog
{
  Q_OBJECT
public:
  explicit ExtractDialog(QWidget *parent = nullptr);
  void setArchivePath(const QString &path);
  void setDefaultOutputDir(const QString &dir);
  ExtractOptions options() const;

private slots:
  void browse();
  void updatePasswordState();

private:
  QString m_archivePath;
  QLineEdit *m_dir = nullptr;
  QComboBox *m_pathMode = nullptr;
  QComboBox *m_overwrite = nullptr;
  QLineEdit *m_password = nullptr;
  QCheckBox *m_showPassword = nullptr;
};
