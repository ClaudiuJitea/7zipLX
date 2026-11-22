#pragma once

#include <QDialog>
#include <QStringList>

class QPlainTextEdit;

class HashResultDialog : public QDialog
{
  Q_OBJECT
public:
  explicit HashResultDialog(const QString &title, const QStringList &lines, QWidget *parent = nullptr);

private:
  QPlainTextEdit *m_text = nullptr;
};
