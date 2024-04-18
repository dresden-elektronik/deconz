/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_SETTINGS_DIALOG_H
#define ZM_SETTINGS_DIALOG_H

#include <QDialog>

namespace Ui {
    class zmSettingsDialog;
}

class QModelIndex;
class QAbstractButton;
class QPushButton;
class zmSettingsZcldb;

class zmSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit zmSettingsDialog(QWidget *parent = 0);
    ~zmSettingsDialog();

private Q_SLOTS:
    void categoryClicked(const QModelIndex &index);
    void dataChanged();
    void buttonClicked(QAbstractButton *button);

Q_SIGNALS:
    void saveClicked();

private:
    Ui::zmSettingsDialog *ui;
    QPushButton *m_okButton;
    zmSettingsZcldb *m_zcldb;
};

#endif // ZM_SETTINGS_DIALOG_H
