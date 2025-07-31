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
#include <QProxyStyle>

enum ThemeColor
{
    ColorNodeBase,
    ColorNodeIndicatorBackground,
    ColorNodeIndicatorRx,
    ColorNodeViewBackground,
    ColorSourceRouteStart,
    ColorSourceRouteEnd,
    ColorNodeEndDeviceText,
    ColorServerCluster,
    ColorUrls
};

enum ThemeValue
{
    ThemeValueDeviceNodesV2
};

void Theme_Init();
void Theme_Destroy();

int Theme_TextWidth(const QFontMetrics &fm, const QString &str);
QFont Theme_FontMonospace();
QFont Theme_FontRegular();
void Theme_Activate(const QString &theme);
QColor Theme_Color(ThemeColor color);
int Theme_Value(ThemeValue value);

// https://www.olivierclero.com/code/custom-qstyle/
// https://github.com/oclero/qlementine/blob/dev/lib/src/style/QlementineStyle.cpp

using ASuper = QProxyStyle;

class AStyle : public ASuper
{
    Q_OBJECT

public:
    explicit AStyle(const QString &theme, QStyle *parent = nullptr);

    void polish(QApplication *app) override;
    void polish(QWidget *widget) override;
    void polish(QPalette &pal) override;
    void unpolish(QWidget *widget) override;
    void unpolish(QApplication *app) override;
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr) const override;
    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter, const QWidget *widget = nullptr) const override;
    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr, const QWidget *widget = nullptr) const override;
    QPalette standardPalette() const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl subControl, const QWidget *widget) const override;

protected:
};

#endif // GUI_THEME_H
