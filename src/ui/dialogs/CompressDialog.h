#pragma once

#include <QDialog>
#include <QStringList>

#include "backend/CompressOperation.h"

class QLineEdit;
class QComboBox;
class QCheckBox;
class QSpinBox;

class CompressDialog : public QDialog
{
  Q_OBJECT
public:
  explicit CompressDialog(const QStringList &files, const QString &defaultDir = QString(), QWidget *parent = nullptr);
  CompressOptions options() const;

private slots:
  void browse();
  void formatChanged();

private:
  QStringList m_files;
  QLineEdit *m_archivePath = nullptr;
  QComboBox *m_format = nullptr;
  QComboBox *m_level = nullptr;
  QLineEdit *m_password = nullptr;
  QCheckBox *m_encryptHeaders = nullptr;
  QComboBox *m_volume = nullptr;
  QSpinBox *m_threads = nullptr;
  QLineEdit *m_method = nullptr;
  QLineEdit *m_dictionary = nullptr;
  QCheckBox *m_solid = nullptr;
  QString m_lastExtension;
};
