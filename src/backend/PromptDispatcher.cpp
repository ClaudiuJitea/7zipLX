#include "PromptDispatcher.h"
#include "InputDialog.h"
#include "MessageDialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QLocale>
#include <QPushButton>
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
    const QString dialogTitle = title.isEmpty() ? tr("Password required") : title;
    accepted = InputDialog::getText(QApplication::activeWindow(), dialogTitle, dialogTitle,
                                    tr("Password:"), &value, true);
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
    MessageDialog dialog(QApplication::activeWindow());
    dialog.setWindowTitle(tr("Confirm File Replace"));
    dialog.setKind(MessageDialog::Kind::Question);
    dialog.setHeading(tr("Replace file?"));
    const QString existingTime = info.existingTime.isValid() ? QLocale().toString(info.existingTime, QLocale::ShortFormat) : tr("unknown");
    const QString newTime = info.newTime.isValid() ? QLocale().toString(info.newTime, QLocale::ShortFormat) : tr("unknown");
    dialog.setText(tr("<b>%1</b> already exists.<br><br>"
                      "<span style='color:#9aa2ad'>Existing</span><br>%2 &middot; %3<br><br>"
                      "<span style='color:#9aa2ad'>New</span><br>%4 &middot; %5<br><br>"
                      "Do you want to replace it?")
                       .arg(info.existingName.toHtmlEscaped(),
                            sizeText(info.existingSize, info.existingSizeKnown),
                            existingTime.toHtmlEscaped(),
                            sizeText(info.newSize, info.newSizeKnown),
                            newTime.toHtmlEscaped()),
                   true);
    QPushButton *yes = dialog.addButton(tr("Yes"), QDialogButtonBox::AcceptRole);
    QPushButton *yesAll = dialog.addButton(tr("Yes to All"), QDialogButtonBox::AcceptRole);
    QPushButton *no = dialog.addButton(tr("No"), QDialogButtonBox::RejectRole);
    QPushButton *noAll = dialog.addButton(tr("No to All"), QDialogButtonBox::RejectRole);
    QPushButton *autoRename = dialog.addButton(tr("Auto Rename"), QDialogButtonBox::ActionRole);
    QPushButton *cancel = dialog.addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    dialog.setDefaultButton(yes);
    dialog.exec();
    if (dialog.clickedButton() == yes) answer = OverwriteAnswer::Yes;
    else if (dialog.clickedButton() == yesAll) answer = OverwriteAnswer::YesToAll;
    else if (dialog.clickedButton() == no) answer = OverwriteAnswer::No;
    else if (dialog.clickedButton() == noAll) answer = OverwriteAnswer::NoToAll;
    else if (dialog.clickedButton() == autoRename) answer = OverwriteAnswer::AutoRename;
    else if (dialog.clickedButton() == cancel) answer = OverwriteAnswer::Cancel;
  });
  return answer;
}

bool PromptDispatcher::confirm(const QString &title, const QString &text, bool dangerous)
{
  bool accepted = false;
  onMain([&] {
    accepted = MessageDialog::confirm(QApplication::activeWindow(), title, title, text, dangerous);
  });
  return accepted;
}

void PromptDispatcher::showError(const QString &title, const QString &text)
{
  onMain([&] {
    MessageDialog::show(QApplication::activeWindow(), title, title, text,
                        MessageDialog::Kind::Error);
  });
}

void PromptDispatcher::showInfo(const QString &title, const QString &text)
{
  onMain([&] {
    MessageDialog::show(QApplication::activeWindow(), title, title, text,
                        MessageDialog::Kind::Information);
  });
}
