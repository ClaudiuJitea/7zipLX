#include "AboutDialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent)
{
  setWindowTitle(QStringLiteral("About 7zipLX"));
  setWindowIcon(QIcon());
  setMinimumWidth(520);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(22, 20, 22, 18);
  layout->setSpacing(10);

  auto *title = new QLabel(QStringLiteral("7zipLX"), this);
  title->setObjectName(QStringLiteral("dialogHeading"));
  layout->addWidget(title);

  auto *sub = new QLabel(QStringLiteral("A native Linux GUI for the 7-Zip archive engine."), this);
  sub->setWordWrap(true);
  layout->addWidget(sub);

  auto *version = new QLabel(QStringLiteral("Version 0.1.0 · Qt %1 · 7-Zip 26.06").arg(QString::fromLatin1(qVersion())), this);
  version->setObjectName(QStringLiteral("dialogSubheading"));
  layout->addWidget(version);

  auto *body = new QLabel(this);
  body->setTextFormat(Qt::RichText);
  body->setWordWrap(true);
  body->setText(QStringLiteral(
      "<p>7zipLX reuses the portable 7-Zip engine for archive formats, "
      "compression, extraction, testing, hashing and archive editing.</p>"
      "<p>Built with Qt 6 Widgets. The application is independent of the "
      "Windows 7-Zip GUI and contains no Win32 UI code.</p>"
      "<p style='color:#a4aab3'>7-Zip Copyright (C) 1999–2026 Igor Pavlov.<br>"
      "7-Zip is licensed under the GNU LGPL. RAR code has additional restrictions.</p>"));
  layout->addWidget(body);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
