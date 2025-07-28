/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_ABOUT_DIALOG_H
#define ZM_ABOUT_DIALOG_H

#include <QDialog>

namespace Ui {
    class zm_about_dialog;
}

class zmAboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit zmAboutDialog(QWidget *parent = 0);
    ~zmAboutDialog();

public Q_SLOTS:
    void linkActivated(const QString &link);

protected:
    void showEvent(QShowEvent *event) override;
    bool event(QEvent *event) override;

private:
    void updateStyle();
    Ui::zm_about_dialog *ui;
};

#endif // ZM_ABOUT_DIALOG_H
