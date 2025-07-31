/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QApplication>
#include <QDebug>
#include <QFontDatabase>
#include <QStyleOption>
#include <QStyleOptionHeader>
#include <QDockWidget>
#include <QGroupBox>
#include <QLayout>
#include <QScrollArea>
#include <QTreeView>
#include <QToolBar>
#include <QPainter>
#include "u_assert.h"
#include "dbg_trace.h"
#include "theme.h"

struct Theme {
    QFont monospace;
    QFont regular;
    QPalette lightPalette;
    QPalette darkPalette;
    QPalette palette;
    QString name;
    qreal roundRadius;
};

static Theme *_theme;

static QRgb colorNodeBase = 0xFFefefef;
static QRgb colorNodeIndicatorBackground = 0xFFe0e0e0;
static QRgb colorNodeIndicatorRx = 0xFF1020FF;
static QRgb colorNodeViewBackground = 0xFFfafafa;
static QRgb colorSourceRouteStart = 0xFFefefef;
static QRgb colorSourceRouteEnd = 0xFFefefef;
static QRgb colorNodeEndDeviceText = 0xFFefefef;
static QRgb colorServerCluster = 0xFF1240ab;
static QRgb colorUrls = 0xFF3232f7;
static int themeValueDeviceNodesV2 = 0;


#ifdef Q_OS_DARWIN
static const qreal qstyleBaseDpi = 72;
#else
static const qreal qstyleBaseDpi = 96;
#endif

static inline qreal getDpr(const QPainter *painter)
{
    Q_ASSERT(painter && painter->device());
    return painter->device()->devicePixelRatio();
}

// static qreal dpi(const QStyleOption *option)
// {
// #ifndef Q_OS_DARWIN
//     // Prioritize the application override, except for on macOS where
//     // we have historically not supported the AA_Use96Dpi flag.
//     if (QCoreApplication::testAttribute(Qt::AA_Use96Dpi))
//         return 96;
// #endif

//     // Expect that QStyleOption::QFontMetrics::QFont has the correct DPI set
// #if QT_VERSION > QT_VERSION_CHECK(5,14,0)
//     if (option)
//     {
//         qreal dpi = option->fontMetrics.fontDpi();
//         return dpi;
//     }
// #endif

//     // Fall back to historical Qt behavior: hardocded 72 DPI on mac,
//     // primary screen DPI on other platforms.
// #ifdef Q_OS_DARWIN
//     return qstyleBaseDpi;
// #else
//     return 96;
// #endif
// }

static qreal dpiScaled(qreal value, qreal dpi)
{
    return value * dpi / qstyleBaseDpi;
}

static qreal dpiScaled(qreal value, const QPaintDevice *device)
{
    return dpiScaled(value, device->logicalDpiX());
}

// static qreal dpiScaled(qreal value, const QStyleOption *option)
// {
//     return dpiScaled(value, dpi(option));
// }


void Theme_Init()
{
    if (!_theme)
    {
        _theme = new Theme;
        _theme->regular = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        _theme->monospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        _theme->roundRadius = 3.0;

        QFontDatabase fdb;
        // this looks pretty good on all platforms
        // especially on macOS where the default is way too bold compared to regular font
        if (fdb.hasFamily("Source Code Pro"))
        {
            _theme->monospace = fdb.font("Source Code Pro", "", _theme->regular.pointSize());
        }
        _theme->monospace.setPointSizeF(_theme->regular.pointSize());

        _theme->darkPalette = QApplication::palette();
        _theme->lightPalette = QApplication::palette();

        // Hex values represent: ARGB
        {
            QPalette &pal = _theme->lightPalette;
            pal.setColor(QPalette::Button, 0xFFf4f4f4);
            pal.setColor(QPalette::ButtonText, 0xFF222222);
            pal.setColor(QPalette::Light, 0xFFfafafa); // lighter than button
            pal.setColor(QPalette::Midlight, 0xFFf8f8f8); // between button and light
            pal.setColor(QPalette::Mid, 0xFFf2f2f2); // between button and dark
            pal.setColor(QPalette::Dark, 0xFFb2b2b2); // darker than button
            pal.setColor(QPalette::Shadow, 0xFF444444); // darkest

            pal.setColor(QPalette::Window, 0xFFd8d8d8);
            pal.setColor(QPalette::WindowText, 0xFF222222);
            pal.setColor(QPalette::Disabled, QPalette::WindowText, 0xFF999999);
            pal.setColor(QPalette::Text, 0xFF222222);
            pal.setColor(QPalette::Disabled, QPalette::Text, 0xFF999999);

            pal.setColor(QPalette::Base, 0xFFfefefe);
            pal.setColor(QPalette::AlternateBase, 0xFFeaeaea);

            pal.setColor(QPalette::Link, 0xFF20a4f1);
            pal.setColor(QPalette::LinkVisited, 0xFF20a4f1);

            pal.setColor(QPalette::Highlight, 0xFF006AD1);
            pal.setColor(QPalette::HighlightedText, 0xFFfafafa);

            pal.setColor(QPalette::NoRole, Qt::cyan);
        }

        {
            QPalette &pal = _theme->darkPalette;
            pal.setColor(QPalette::ButtonText, 0xFFdfdfdf);
            pal.setColor(QPalette::Light, 0xFF606060);
            pal.setColor(QPalette::Midlight, 0xFF575757);
            pal.setColor(QPalette::Button, 0xFF434343);
            pal.setColor(QPalette::Mid, 0xFF343434); // between dark and button
            pal.setColor(QPalette::Dark, 0xFF272727); // darker than button
            pal.setColor(QPalette::Shadow, 0xFF101010);

            pal.setColor(QPalette::BrightText, 0xFFfafafa);
            pal.setColor(QPalette::Window, 0xFF232323);

            pal.setColor(QPalette::WindowText, 0xFFe4e4e5);
            pal.setColor(QPalette::Disabled, QPalette::WindowText, 0xFF848485);
            pal.setColor(QPalette::Text, 0xFFe4e4e5);
            pal.setColor(QPalette::Disabled, QPalette::Text, 0xFF848485);

            pal.setColor(QPalette::Base, 0xFF282828);
            pal.setColor(QPalette::Disabled, QPalette::Base, 0xFF313131);
            pal.setColor(QPalette::AlternateBase, 0xFF2f2f2f);
            pal.setColor(QPalette::Disabled, QPalette::AlternateBase, 0xFF343434);

            pal.setColor(QPalette::Link, 0xFF20a4f1);
            pal.setColor(QPalette::LinkVisited, 0xFF20a4f1);

            pal.setColor(QPalette::Highlight, 0xFF3058b7);
            pal.setColor(QPalette::HighlightedText, 0xFFfafafa);

            pal.setColor(QPalette::Disabled, QPalette::Highlight, 0xFF646464);

            pal.setColor(QPalette::NoRole, Qt::darkRed);
        }
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

void Theme_Activate(const QString &theme)
{
    if (!_theme)
        return;

    if (theme == "light")
    {
        _theme->palette = _theme->lightPalette;
        colorNodeBase = 0xFFefefef;
        colorNodeIndicatorBackground = 0xFFe0e0e0;
        colorNodeIndicatorRx = 0xFF0000FF;
        colorNodeViewBackground = 0xFFfafafa;
        colorSourceRouteStart = 0xFF2060ba;
        colorSourceRouteEnd = 0xFFba6020;
        colorNodeEndDeviceText = _theme->palette.color(QPalette::WindowText).rgba();
        colorServerCluster = 0xFF1240ab;
        colorUrls = 0xFF20a4f1;
        themeValueDeviceNodesV2 = 0;
    }
    else if (theme == "dark")
    {
        _theme->palette = _theme->darkPalette;
        //colorNodeBase = 0xFF202020;
        colorNodeBase = 0xFF282828;
        colorNodeIndicatorBackground = 0xFF404040;
        colorNodeIndicatorRx = 0xFF20a4ff;
        colorNodeViewBackground = 0xFF383838;
        colorSourceRouteStart = 0xFF2060ba;
        colorSourceRouteEnd = 0xFFba6020;
        colorNodeEndDeviceText = _theme->palette.color(QPalette::WindowText).rgba();
        colorServerCluster = 0xFF20a4f1;
        colorUrls = 0xFF20a4f1;
        themeValueDeviceNodesV2 = 1;
    }
    else
    {
        U_ASSERT(0 && "unsupported theme");
        return;
    }

    _theme->name = theme;
}

QColor Theme_Color(ThemeColor color)
{
    switch (color)
    {
    case ColorNodeBase: return colorNodeBase;
    case ColorNodeIndicatorBackground: return colorNodeIndicatorBackground;
    case ColorNodeIndicatorRx: return colorNodeIndicatorRx;
    case ColorNodeViewBackground: return colorNodeViewBackground;
    case ColorSourceRouteStart: return colorSourceRouteStart;
    case ColorSourceRouteEnd: return colorSourceRouteEnd;
    case ColorNodeEndDeviceText: return colorNodeEndDeviceText;
    case ColorServerCluster: return colorServerCluster;
    case ColorUrls: return colorUrls;
    default: break;
    }

    U_ASSERT(0 && "color not in switch statement");
    return Qt::magenta; // to easily spot unhandled values
}

int Theme_Value(ThemeValue value)
{
    if (value == ThemeValueDeviceNodesV2)
        return themeValueDeviceNodesV2;

    return 0;
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

AStyle::AStyle(const QString &theme, QStyle *parent) :
    ASuper(parent)
{
}

void AStyle::polish(QApplication *app)
{
    ASuper::polish(app);
}

void AStyle::polish(QWidget *widget)
{
    QPalette pal = _theme->palette;

    {
        // custom widget properties
        // theme.bgrole: sets a custom QPalette::ColorRole for QPalette::Window
        QVariant bg = widget->property("theme.bgrole");
        if (bg.isValid())
        {
            pal.setColor(QPalette::Window, pal.color((QPalette::ColorRole)bg.toInt()));
        }
    }

    if (qobject_cast<const QDockWidget*>(widget))
    {
    }
    else if (qobject_cast<const QDockWidget*>(widget->parentWidget()))
    {
        pal.setColor(QPalette::Window, _theme->palette.color(QPalette::Mid));
    }
    else if (qobject_cast<const QGroupBox*>(widget))
    {
        pal.setColor(QPalette::Window, _theme->palette.color(QPalette::Dark));
    }

    ASuper::polish(widget);
    widget->setPalette(pal);
}

void AStyle::polish(QPalette &pal)
{
    ASuper::polish(pal);
}

void AStyle::unpolish(QWidget *widget)
{
    ASuper::unpolish(widget);
}

void AStyle::unpolish(QApplication *app)
{
    ASuper::unpolish(app);
}

void AStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    if (element == PE_FrameTabBarBase)
    {
        // the shadow line above tabs of QDockWidget
        painter->setPen(Qt::NoPen);
        painter->setBrush(_theme->palette.color(QPalette::Mid));
        painter->drawRect(option->rect);
        return;
    }

    if (element == PE_PanelButtonBevel || element == PE_PanelButtonCommand)
    {
        if (const QStyleOptionButton *opt = qstyleoption_cast<const QStyleOptionButton*>(option))
        {
            QColor color = option->palette.color(QPalette::Button);
            if (opt->state & State_Sunken)
            {
                color = option->palette.color(QPalette::Light);
            }
            else if (opt->state & State_MouseOver)
            {
                color = option->palette.color(QPalette::Midlight);
            }

            qreal lineWidth = dpiScaled(1.0, painter->device());
            qreal M = dpiScaled(1.0, painter->device());
            QRect rect = option->rect.marginsRemoved(QMargins(M,M,M,M));
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setBrush(color);
            // if (opt->features & QStyleOptionButton::Flat || !(opt->features & QStyleOptionButton::HasMenu))
            // {
            //     painter->setPen(Qt::NoPen);
            //     painter->drawRoundedRect(rect, _theme->roundRadius, _theme->roundRadius);
            // }

            painter->translate(0.5, 0.5);

            if (opt->state & State_On)
            {
                painter->setPen(QPen(option->palette.color(QPalette::Highlight), lineWidth));
            }
            else
            {
                painter->setPen(QPen(option->palette.color(QPalette::Dark), lineWidth));
            }
            painter->drawRoundedRect(rect, _theme->roundRadius, _theme->roundRadius);

            painter->restore();
            return;
        }
    }

#if 0
    case PE_PanelButtonCommand:
    case PE_PanelButtonBevel:
    case PE_PanelButtonTool:
    case PE_IndicatorButtonDropDown:
        qDrawShadePanel(p, opt->rect, opt->palette,
                        opt->state & (State_Sunken | State_On), 1,
                        &opt->palette.brush(QPalette::Button));
        break;
#endif

    if (element == PE_FrameTabWidget)
    {
        painter->save();
        const QColor fillColor = _theme->palette.color(QPalette::Mid);
        painter->fillRect(option->rect.adjusted(0, 0, 0, 0), fillColor);
        painter->restore();
        return;
    }

    ASuper::drawPrimitive(element, option, painter, widget);
}

void AStyle::drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    if (element == CE_TabBarTabShape)
    {
        if (const QStyleOptionTab *tab = qstyleoption_cast<const QStyleOptionTab*>(option))
        {
            QStyleOptionTab opt1 = *tab;

            tab = &opt1;

            painter->save();
            {
                bool rtlHorTabs = (tab->direction == Qt::RightToLeft
                                   && (tab->shape == QTabBar::RoundedNorth
                                       || tab->shape == QTabBar::RoundedSouth));
                bool selected = tab->state & State_Selected;
                bool lastTab = ((!rtlHorTabs && tab->position == QStyleOptionTab::End)
                                || (rtlHorTabs
                                    && tab->position == QStyleOptionTab::Beginning));
                bool onlyOne = tab->position == QStyleOptionTab::OnlyOneTab;
                int tabOverlap = pixelMetric(PM_TabBarTabOverlap, option, widget);
                QRect rect = option->rect.adjusted(0, 0, (onlyOne || lastTab) ? 0 : tabOverlap, 0);

                QTransform rotMatrix;
                bool flip = false;
                //painter->setPen(QFusionStylePrivate::darkShade);
                painter->setPen(Qt::NoPen);

                switch (tab->shape) {
                case QTabBar::RoundedNorth:
                    break;
                case QTabBar::RoundedSouth:
                    rotMatrix.rotate(180);
                    rotMatrix.translate(0, -rect.height() + 1);
                    rotMatrix.scale(-1, 1);
                    painter->setTransform(rotMatrix, true);
                    break;
                case QTabBar::RoundedWest:
                    rotMatrix.rotate(180 + 90);
                    rotMatrix.scale(-1, 1);
                    flip = true;
                    painter->setTransform(rotMatrix, true);
                    break;
                case QTabBar::RoundedEast:
                    rotMatrix.rotate(90);
                    rotMatrix.translate(0, - rect.width() + 1);
                    flip = true;
                    painter->setTransform(rotMatrix, true);
                    break;
                default:
                    painter->restore();
                    QCommonStyle::drawControl(element, tab, painter, widget);
                    return;
                }

                if (flip)
                    rect = QRect(rect.y(), rect.x(), rect.height(), rect.width());

                painter->setRenderHint(QPainter::Antialiasing, true);
                painter->translate(0.5, 0.5);

                QColor fillColor;
                if (selected) {
                    fillColor = _theme->palette.color(QPalette::Mid);
                } else {
                    fillColor = _theme->palette.color(QPalette::Dark);
                }

                QRect drawRect = rect.adjusted(0, selected ? 0 : 2, 0, 3);
                painter->save();
                painter->setClipRect(rect.adjusted(-1, -1, 1, selected ? -2 : -3));
                painter->setBrush(fillColor);
                painter->drawRoundedRect(drawRect.adjusted(0, 0, -1, -1), _theme->roundRadius, _theme->roundRadius);
                painter->restore();
            }
            painter->restore();
            return;
        }
    }
    else if (element == CE_ToolBar)
    {
        // the style only draws the background with a solid color
        painter->setPen(Qt::NoPen);
        const int bri = (_theme->palette.color(QPalette::Window).red() + _theme->palette.color(QPalette::Mid).red()) / 2;
        painter->setBrush(QBrush(QColor(bri, bri, bri)));
        painter->drawRect(option->rect);
        return;
    }
    else if (element == CE_HeaderSection)
    {
        // Draws the header in tables.
        if (const QStyleOptionHeader *header = qstyleoption_cast<const QStyleOptionHeader *>(option))
        {
            painter->save();

            const QRect rect = option->rect;

            painter->fillRect(option->rect, widget->palette().button());

            if (header->orientation == Qt::Horizontal &&
                header->position != QStyleOptionHeader::End &&
                header->position != QStyleOptionHeader::OnlyOneSection)
            {
                painter->setPen(QPen(widget->palette().base().color()));
                painter->drawLine(rect.topRight(), rect.bottomRight());
            }
            else if (header->orientation == Qt::Vertical)
            {
                painter->setPen(QPen(widget->palette().base().color()));
                painter->drawLine(rect.topRight(), rect.bottomRight());
            }

            painter->restore();
            return;
        }
    }
    else if (element == CE_HeaderLabel)
    {
        if (const QStyleOptionHeader *header = qstyleoption_cast<const QStyleOptionHeader *>(option))
        {
            QString text = header->text;
            drawItemText(painter, header->rect, header->textAlignment, header->palette,
                                  header->state.testFlag(State_Enabled), text, QPalette::ButtonText);
            return;
        }
    }

    ASuper::drawControl(element, option, painter, widget);
}

void AStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter, const QWidget *widget) const
{
    if (control == CC_GroupBox)
    {
        painter->save();
        if (const QStyleOptionGroupBox *groupBox = qstyleoption_cast<const QStyleOptionGroupBox *>(option)) {
            // Draw frame
            QRect textRect = proxy()->subControlRect(CC_GroupBox, option, SC_GroupBoxLabel, widget);
            QRect checkBoxRect = proxy()->subControlRect(CC_GroupBox, option, SC_GroupBoxCheckBox, widget);

            if (groupBox->subControls & QStyle::SC_GroupBoxFrame) {
                QStyleOptionFrame frame;
                frame.QStyleOption::operator=(*groupBox);
                frame.features = groupBox->features;
                frame.lineWidth = groupBox->lineWidth;
                frame.midLineWidth = groupBox->midLineWidth;
                frame.rect = proxy()->subControlRect(CC_GroupBox, option, SC_GroupBoxFrame, widget);
                painter->save();

#if 0 // bright header, dark content fill
                painter->setPen(Qt::NoPen);
                QMargins margins(3, 3, 3, 3);
                QRect rectWithoutMargins = frame.rect.marginsRemoved(margins);

                // header background
                QRect r1(textRect);
                r1.setRight(groupBox->rect.left());
                r1.setLeft(groupBox->rect.right());
                r1.setBottom(textRect.bottom() + 1);
                painter->setClipRect(r1);
                painter->setBrush(_theme->palette.midlight());
                painter->drawRoundedRect(rectWithoutMargins, 2, 2);

                // content background
                r1.setTop(textRect.bottom() + 1);
                r1.setBottom(groupBox->rect.bottom());
                painter->setClipRect(r1);
                painter->setBrush(_theme->palette.dark());
                painter->drawRoundedRect(rectWithoutMargins, 2, 2);

                painter->setBrush(Qt::NoBrush);
                painter->setPen(QPen(_theme->palette.color(QPalette::Dark).darker(125), 1));
                painter->drawLine(r1.topLeft(), r1.topRight());
#endif

#if 1
                QMargins margins(3, 3, 3, 3);
                QRect rectWithoutMargins = frame.rect.marginsRemoved(margins);
                rectWithoutMargins.setTop(textRect.bottom() + 1);
                painter->setPen(QPen(_theme->palette.color(QPalette::Window), 2));
                painter->setBrush(Qt::NoBrush);
                painter->drawRoundedRect(rectWithoutMargins, _theme->roundRadius, _theme->roundRadius);
#endif
                painter->restore();
            }

            // Draw title
            if ((groupBox->subControls & QStyle::SC_GroupBoxLabel) && !groupBox->text.isEmpty()) {
                // groupBox->textColor gets the incorrect palette here
                painter->setPen(QPen(option->palette.windowText(), 1));
                int alignment = int(groupBox->textAlignment);
                if (!proxy()->styleHint(QStyle::SH_UnderlineShortcut, option, widget))
                    alignment |= Qt::TextHideMnemonic;

                //proxy()->drawItemText(painter, textRect,  Qt::TextShowMnemonic | Qt::AlignLeft | alignment,
                //                      groupBox->palette, groupBox->state & State_Enabled, groupBox->text, QPalette::NoRole);

                // QPen savedPen = painter->pen();
                //painter->setBrush(Qt::NoBrush);

                painter->drawText(textRect, Qt::TextShowMnemonic | Qt::AlignLeft | alignment, groupBox->text);
                // painter->setPen(savedPen);


                if (groupBox->state & State_HasFocus) {
                    QStyleOptionFocusRect fropt;
                    fropt.QStyleOption::operator=(*groupBox);
                    fropt.rect = textRect.adjusted(-2, -1, 2, 1);
                    proxy()->drawPrimitive(PE_FrameFocusRect, &fropt, painter, widget);
                }
            }

            // Draw checkbox
            if (groupBox->subControls & SC_GroupBoxCheckBox) {
                QStyleOptionButton box;
                box.QStyleOption::operator=(*groupBox);
                box.rect = checkBoxRect;
                proxy()->drawPrimitive(PE_IndicatorCheckBox, &box, painter, widget);
            }
        }
        painter->restore();
        return;
    }
    else if (control == CC_ScrollBar)
    {
        if (const QStyleOptionSlider *scrollBar = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
            painter->save();

            painter->fillRect(option->rect, scrollBar->palette.base());

            QRect scrollBarSubLine = subControlRect(control, scrollBar, SC_ScrollBarSubLine, widget);
            QRect scrollBarAddLine = subControlRect(control, scrollBar, SC_ScrollBarAddLine, widget);
            QRect scrollBarSlider = subControlRect(control, scrollBar, SC_ScrollBarSlider, widget);
            QRect scrollBarGroove = subControlRect(control, scrollBar, SC_ScrollBarGroove, widget);
            const bool vertical = option->rect.width() < option->rect.height();

//            painter->fillRect(scrollBarGroove, Qt::green);

            // adapt slider to cover +/- boxes space

            if (!vertical)
            {
                scrollBarSlider.setLeft(scrollBarSlider.left() - scrollBarAddLine.width());
                scrollBarSlider.setRight(scrollBarSlider.right() + scrollBarSubLine.width());
            }
            else
            {
                scrollBarSlider.setTop(scrollBarSlider.top() - scrollBarAddLine.height());
                scrollBarSlider.setBottom(scrollBarSlider.bottom() + scrollBarSubLine.height());
            }
            //scrollBarSlider = scrollBarSlider.marginsRemoved(QMargins(1,1,1,1));
            scrollBarSlider = scrollBarSlider.marginsRemoved(QMargins(2,2,2,2));

            painter->setPen(Qt::NoPen);
            if (scrollBar->state & QStyle::State_MouseOver)
            {
                painter->setBrush(scrollBar->palette.midlight());
            }
            else
            {
                painter->setBrush(scrollBar->palette.button());
            }

            painter->drawRoundedRect(scrollBarSlider, _theme->roundRadius, _theme->roundRadius);

            painter->restore();

            return;
        }
    }

    ASuper::drawComplexControl(control, option, painter, widget);
}

int AStyle::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    return ASuper::pixelMetric(metric, option, widget);
}

QPalette AStyle::standardPalette() const
{
    return _theme->palette;
}

QRect AStyle::subControlRect(ComplexControl control, const QStyleOptionComplex *option,
                                   SubControl subControl, const QWidget *widget) const
{
    if (control == CC_GroupBox)
    {
        if (subControl == SC_GroupBoxLabel)
        {
            QRect rect = ASuper::subControlRect(control, option, subControl, widget);
            if (widget) // normally group box header text is with zero margin to the left
            { // adjust margin to be the same as content
                int dx = widget->layout()->contentsMargins().left();
                rect.moveLeft(dx);
            }
            return rect;

        }
#if 0
        if (const QStyleOptionGroupBox *groupBox = qstyleoption_cast<const QStyleOptionGroupBox *>(option)) {
            const int groupBoxTextAlignment = groupBox->textAlignment;
            const bool hasVerticalAlignment = (groupBoxTextAlignment & Qt::AlignVertical_Mask) == Qt::AlignVCenter;
            const int fontMetricsHeight = groupBox->text.isEmpty() ? 0 : groupBox->fontMetrics.height();

            if (subControl == SC_GroupBoxFrame)
                return rect;
            else if (subControl == SC_GroupBoxContents) {
                QRect frameRect = option->rect.adjusted(0, 0, 0, -groupBoxBottomMargin);
                int margin = 3;
                int leftMarginExtension = 0;
                const int indicatorHeight = option->subControls.testFlag(SC_GroupBoxCheckBox) ?
                                                        pixelMetric(PM_IndicatorHeight, option, widget) : 0;
                const int topMargin = qMax(indicatorHeight, fontMetricsHeight) +
                                        groupBoxTopMargin;
                return frameRect.adjusted(leftMarginExtension + margin, margin + topMargin, -margin, -margin - groupBoxBottomMargin);
            }

            QSize textSize = option->fontMetrics.boundingRect(groupBox->text).size() + QSize(2, 2);
            int indicatorWidth = proxy()->pixelMetric(PM_IndicatorWidth, option, widget);
            int indicatorHeight = proxy()->pixelMetric(PM_IndicatorHeight, option, widget);

            const int width = textSize.width()
                + (option->subControls & QStyle::SC_GroupBoxCheckBox ? indicatorWidth + 5 : 0);

            rect = QRect();

            if (option->rect.width() > width) {
                switch (groupBoxTextAlignment & Qt::AlignHorizontal_Mask) {
                case Qt::AlignHCenter:
                    rect.moveLeft((option->rect.width() - width) / 2);
                    break;
                case Qt::AlignRight:
                    rect.moveLeft(option->rect.width() - width
                                  - (hasVerticalAlignment ? proxy()->pixelMetric(PM_LayoutRightMargin, groupBox, widget) : 0));
                    break;
                case Qt::AlignLeft:
                    if (hasVerticalAlignment)
                        rect.moveLeft(proxy()->pixelMetric(PM_LayoutLeftMargin, option, widget));
                    break;
                }
            }

            if (subControl == SC_GroupBoxCheckBox) {
                rect.setWidth(indicatorWidth);
                rect.setHeight(indicatorHeight);
                rect.moveTop(textSize.height() > indicatorHeight ? (textSize.height() - indicatorHeight) / 2 : 0);
                rect.translate(1, 0);
            } else if (subControl == SC_GroupBoxLabel) {
                rect.setSize(textSize);
                rect.moveTop(1);
                if (option->subControls & QStyle::SC_GroupBoxCheckBox)
                    rect.translate(indicatorWidth + 5, 0);
            }
            return visualRect(option->direction, option->rect, rect);
        }

        return rect;
#endif
    }

    return ASuper::subControlRect(control, option, subControl, widget);
}

int Theme_TextWidth(const QFontMetrics &fm, const QString &str)
{
#if (QT_VERSION < QT_VERSION_CHECK(5, 11, 0))
    return fm.width(str);
#else
    return fm.horizontalAdvance(str);
#endif
}
