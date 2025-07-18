/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef GUI_THEME_H
#define GUI_THEME_H

#include <QFont>

void Theme_Init();
void Theme_Destroy();

QFont Theme_FontMonospace();
QFont Theme_FontRegular();

#endif // GUI_THEME_H
