/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_APP_H
#define ZM_APP_H

#include <QApplication>

extern bool gHeadlessVersion;


class AppPrivate;
class zmApp : public QApplication
{
    Q_OBJECT

public:
    zmApp(int &argc, char **argv);
    ~zmApp();

private Q_SLOTS:
    void eventQueueIdle();

private:
    AppPrivate *d_ptr = nullptr;
};

#endif // ZM_APP_H
