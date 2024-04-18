/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef SOURCE_ROUTE_INFO_H
#define SOURCE_ROUTE_INFO_H

#include <QWidget>

namespace Ui {
class SourceRouteInfo;
}

class SourceRouteInfo : public QWidget
{
    Q_OBJECT

public:
    explicit SourceRouteInfo(QWidget *parent = nullptr);
    ~SourceRouteInfo();

private:
    Ui::SourceRouteInfo *ui;
};

#endif // SOURCE_ROUTE_INFO_H
