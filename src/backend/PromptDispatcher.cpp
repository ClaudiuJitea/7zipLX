#include "PromptDispatcher.h"

#include <QApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QAbstractButton>
#include <QLocale>
#include <QThread>
#include <utility>

namespace {
QString sizeText(quint64 value, bool known)
{
  if (!known)
    return QStringLiteral("unknown");
  const double units[] = {1.0, 1024.0, 1024.0 * 1024.0, 1024.0 * 1024.0 * 1024.0, 1024.0 * 1024.0 * 1024.0 * 1024.0};
  const char *names[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  int i = 0;
  double v = double(value);
  while (i < 4 && v >= 1024.0) { v /= 1024.0; ++i; }
  return QStringLiteral("%1 %2").arg(v, 0, 'f', i == 0 ? 0 : 1).arg(QString::fromLatin1(names[i]));
}
} // namespace

PromptDispatcher::PromptDispatcher(QObject *parent) : QObject(parent) {}

template<typename Fn>
void PromptDispatcher::onMain(Fn &&fn) const
{
  if (QThread::currentThread() == QApplication::instance()->thread())
  {
    fn();
    return;
  }
  QMetaObject::invokeMethod(const_cast<PromptDispatcher *>(this), std::forward<Fn>(fn), Qt::BlockingQueuedConnection);
}

bool PromptDispatcher::askPassword(QString *password, bool *ok, const QString &title)
{
  bool accepted = false;
  QString value;
  onMain([&] {
    bool dialogOk = false;
    const QString text = QInputDialog::getText(
        nullptr,
        title.isEmpty() ? tr("Password required") : title,
        tr("Enter password:"),
        QLineEdit::Password,
        QString(),
        &dialogOk);
    if (dialogOk)
    {
      value = text;
      accepted = true;
    }
  });
  if (password)
    *password = value;
  if (ok)
    *ok = accepted;
  return accepted;
}

OverwriteAnswer PromptDispatcher::askOverwrite(const OverwriteInfo &info)
{
  OverwriteAnswer answer = OverwriteAnswer::Cancel;
  onMain([&] {
    QMessageBox box;
    box.setIcon(QMessageBox::NoIcon);
    box.setWindowTitle(tr("Confirm File Replace"));
    box.setTextFormat(Qt::RichText);
    const QString existingTime = info.existingTime.isValid() ? QLocale().toString(info.existingTime, QLocale::ShortFormat) : tr("unknown");
    const QString newTime = info.newTime.isValid() ? QLocale().toString(info.newTime, QLocale::ShortFormat) : tr("unknown");
    box.setText(tr("<b>%1</b> already exists.<br><br>"
                   "Existing: %2, %3<br>"
                   "New: %4, %5<br><br>"
                   "Do you want to replace it?")
                    .arg(info.existingName.toHtmlEscaped(),
                         sizeText(info.existingSize, info.existingSizeKnown),
                         existingTime.toHtmlEscaped(),
                         sizeText(info.newSize, info.newSizeKnown),
                         newTime.toHtmlEscaped()));
    QPushButton *yes = box.addButton(tr("Yes"), QMessageBox::AcceptRole);
    QPushButton *yesAll = box.addButton(tr("Yes to All"), QMessageBox::AcceptRole);
    QPushButton *no = box.addButton(tr("No"), QMessageBox::RejectRole);
    QPushButton *noAll = box.addButton(tr("No to All"), QMessageBox::RejectRole);
    QPushButton *autoRename = box.addButton(tr("Auto Rename"), QMessageBox::ActionRole);
    QAbstractButton *cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(yes);
    box.exec();
    if (box.clickedButton() == yes) answer = OverwriteAnswer::Yes;
    else if (box.clickedButton() == yesAll) answer = OverwriteAnswer::YesToAll;
    else if (box.clickedButton() == no) answer = OverwriteAnswer::No;
    else if (box.clickedButton() == noAll) answer = OverwriteAnswer::NoToAll;
    else if (box.clickedButton() == autoRename) answer = OverwriteAnswer::AutoRename;
    else if (box.clickedButton() == cancel) answer = OverwriteAnswer::Cancel;
  });
  return answer;
}

bool PromptDispatcher::confirm(const QString &title, const QString &text, bool dangerous)
{
  bool accepted = false;
  onMain([&] {
    QMessageBox box(QMessageBox::NoIcon, title, text, QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(dangerous ? QMessageBox::No : QMessageBox::Yes);
    accepted = (box.exec() == QMessageBox::Yes);
  });
  return accepted;
}

void PromptDispatcher::showError(const QString &title, const QString &text)
{
  onMain([&] {
    QMessageBox box(QMessageBox::NoIcon, title, text, QMessageBox::Ok);
    box.exec();
  });
}

void PromptDispatcher::showInfo(const QString &title, const QString &text)
{
  onMain([&] {
    QMessageBox box(QMessageBox::NoIcon, title, text, QMessageBox::Ok);
    box.exec();
  });
}
