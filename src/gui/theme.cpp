/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QFontDatabase>
#include "theme.h"

struct Theme {
    QFont monospace;
    QFont regular;
};

static Theme *_theme;

void Theme_Init()
{
    if (!_theme)
    {
        _theme = new Theme;
        _theme->regular = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        _theme->monospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);

        QFontDatabase fdb;
        if (fdb.hasFamily("Source Code Pro"))
        {
            _theme->monospace = fdb.font("Source Code Pro", "", _theme->regular.pointSize());
        }
        _theme->monospace.setPointSizeF(_theme->regular.pointSize());

    }
}

void Theme_Destroy()
{
    if (_theme)
    {
        delete _theme;
        _theme = nullptr;
    }
}

QFont Theme_FontMonospace()
{
    if (_theme)
    {
        return _theme->monospace;
    }
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);;
}

QFont Theme_FontRegular()
{
    if (_theme)
    {
        return _theme->regular;
    }
    return QFontDatabase::systemFont(QFontDatabase::GeneralFont);
}
