/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include "source_route_info.h"
#include "ui_source_route_info.h"
#include "zm_controller.h"

SourceRouteInfo::SourceRouteInfo(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SourceRouteInfo)
{
    ui->setupUi(this);

    setAutoFillBackground(true);
    setProperty("theme.bgrole", QPalette::Mid);
    auto *ctrl = deCONZ::controller();

    ui->enableSourceRouting->setChecked(ctrl->sourceRoutingEnabled());
    ui->maxHops->setValue(ctrl->sourceRouteMaxHops());
    ui->minLqi->setValue(ctrl->sourceRouteMinLqi());
    ui->minLqiDisplay->setValue(ctrl->minLqiDisplay());

    connect(ui->enableSourceRouting, &QCheckBox::toggled,
            deCONZ::controller(), &zmController::setSourceRoutingEnabled);

    connect(ui->fastDiscovery, &QCheckBox::toggled,
            deCONZ::controller(), &zmController::setFastNeighborDiscovery);

    connect(ui->maxHops, SIGNAL(valueChanged(int)),
            deCONZ::controller(), SLOT(setSourceRouteMaxHops(int)));

    connect(ui->minLqi, SIGNAL(valueChanged(int)),
            deCONZ::controller(), SLOT(setSourceRouteMinLqi(int)));

    connect(ui->minLqiDisplay, SIGNAL(valueChanged(int)),
            deCONZ::controller(), SLOT(setMinLqiDisplay(int)));
}

SourceRouteInfo::~SourceRouteInfo()
{
    delete ui;
}
