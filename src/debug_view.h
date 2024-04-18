/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef DEBUG_VIEW_H
#define DEBUG_VIEW_H

#include <QDialog>

namespace Ui {
class DebugView;
}

class DebugView : public QDialog
{
    Q_OBJECT

public:
    explicit DebugView(QWidget *parent = nullptr);
    ~DebugView();

    void log(int level, const char *msg);

private Q_SLOTS:
    void checkboxStateChanged(int state);
    void init();

private:
    Ui::DebugView *ui;
};

#endif // DEBUG_VIEW_H
