/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ACTOR_VFS_VIEW_H
#define ACTOR_VFS_VIEW_H

#include <QDialog>

namespace Ui {
class ActorVfsView;
}

class ActorVfsModel;

class ActorVfsView : public QDialog
{
    Q_OBJECT

public:
    explicit ActorVfsView(ActorVfsModel *model, QWidget *parent = nullptr);
    ~ActorVfsView();

private:
    Ui::ActorVfsView *ui;
};

#endif // ACTOR_VFS_VIEW_H
