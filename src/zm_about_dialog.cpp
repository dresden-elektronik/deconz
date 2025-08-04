/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QPushButton>
#include "deconz/u_library_ex.h"
#include "gui/theme.h"
#include "zm_about_dialog.h"
#include "ui_zm_about_dialog.h"
#include "zm_master.h"
#include "config.h"

zmAboutDialog::zmAboutDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::zm_about_dialog)
{
    qint64 msec = qint64(GIT_DATE) * 1000;
    auto sourceDateTime = QDateTime::fromMSecsSinceEpoch(msec);

    ui->setupUi(this);
    setWindowTitle(tr("About") + " " + qApp->applicationName());
    ui->copyrightLabel->setText(tr("Copyright © %1 dresden elektronik ingenieurtechnik gmbh. All rights reserved.").arg(sourceDateTime.date().year()));
    ui->copyrightLabel->setDisabled(true);

    ui->labelLogo->hide(); // doesn't work well with theme, hide for now

    ui->buttonBox->button(QDialogButtonBox::Ok)->setIcon(QIcon());
    connect(ui->buttonBox, SIGNAL(accepted()),
            ui->buttonBox, SLOT(deleteLater()));

    connect(ui->labelWWW, SIGNAL(linkActivated(QString)),
            this, SLOT(linkActivated(QString)));

    connect(ui->labelSupportMail, SIGNAL(linkActivated(QString)),
            this, SLOT(linkActivated(QString)));

    setAutoFillBackground(true);

}

zmAboutDialog::~zmAboutDialog()
{
    delete ui;
}

void zmAboutDialog::linkActivated(const QString &link)
{
    QDesktopServices::openUrl(QUrl(link));
}

void zmAboutDialog::showEvent(QShowEvent *event)
{
    QString devFirmwareVersion;

    if (deCONZ::master()->connected())
    {
        const QString fwVersion = QString("%1").arg(deCONZ::master()->deviceFirmwareVersion(), 8, 16, QLatin1Char('0'));
        devFirmwareVersion = QString("Firmware: 0x%1").arg(fwVersion);
    }
    else
    {
        devFirmwareVersion = "Firmware: not connected";
    }

    QString appVersion;
    QStringList tags = QString(GIT_TAGS).split(";");

    appVersion += qApp->applicationVersion();
    appVersion += tr("\n\nQt: %1").arg(QT_VERSION_STR);

#ifdef __GNUC__
    appVersion += tr("\nGCC: %1.%2.%3 (C++ %4)").arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__).arg(__cplusplus);
#endif

    void *libcrypto = U_library_open_ex("libcrypto");

    unsigned major = 0;
    unsigned minor = 0;
    unsigned patch = 0;
    if (libcrypto)
    {
        unsigned long (*OpenSSL_version_num)(void) = (unsigned long (*)(void))U_library_symbol(libcrypto, "OpenSSL_version_num");

        if (OpenSSL_version_num)
        {
            unsigned long n = OpenSSL_version_num();
            major = (n >> 28) & 0xF;
            minor = (n >> 20) & 0xFF;
            patch = (n >> 12) & 0xFF;
        }

        U_library_close(libcrypto);

        if (major)
        {
            appVersion += QString("\nOpenSSL: %1.%2.%3\n").arg(major).arg(minor).arg(patch);
        }
    }

    appVersion += tr("\n\nCommit: %1").arg(QString(GIT_COMMIT).left(6));
    for (int i = 0; i < tags.size(); i++)
    {
        appVersion += tr("\nTag: %1").arg(tags[i]);
    }
    appVersion += "\n" + devFirmwareVersion + "\n";

    ui->appVersionLabel->setText(appVersion);
    updateStyle();

    QDialog::showEvent(event);
}

bool zmAboutDialog::event(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange && isVisible())
    {
        updateStyle();
    }
    return QDialog::event(event);
}

void zmAboutDialog::updateStyle()
{
    QColor color = palette().link().color();
    QString sheet = QString::fromLatin1("a { text-decoration: none; color: %1; }").arg(color.name());
    ui->labelWWW->setStyleSheet(sheet);
    ui->labelSupportMail->setStyleSheet(sheet);
}
