/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include "actor_vfs_model.h"
#include "actor_vfs_view.h"
#include "ui_actor_vfs_view.h"

ActorVfsView::ActorVfsView(ActorVfsModel *model, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ActorVfsView)
{
    ui->setupUi(this);

    ui->treeView->setModel(model);
    ui->treeView->header()->resizeSection(0, 340);
    ui->treeView->setAlternatingRowColors(true);
}

ActorVfsView::~ActorVfsView()
{
    delete ui;
}
