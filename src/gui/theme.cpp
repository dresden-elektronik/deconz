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
#include <QDockWidget>
#include <QGroupBox>
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
};

static Theme *_theme;

static QRgb colorNodeBase = 0xFFefefef;
static QRgb colorNodeViewBackground = 0xFFfafafa;

void Theme_Init()
{
    if (!_theme)
    {
        _theme = new Theme;
        _theme->regular = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        _theme->monospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);

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
            pal.setColor(QPalette::Button, 0xFFd0d0d0);
            pal.setColor(QPalette::ButtonText, 0xFF222222);
            pal.setColor(QPalette::Light, 0xFFd8d8d8); // lighter than button
            pal.setColor(QPalette::Midlight, 0xFFd4d4d4); // between button and light
            pal.setColor(QPalette::Mid, 0xFFc8c8c8); // between button and dark
            pal.setColor(QPalette::Dark, 0xFFc0c0c0); // darker than button
            pal.setColor(QPalette::Shadow, 0xFF444444); // darkest

            // pal.setColor(QPalette::Window, 0xFFfafafa);
            pal.setColor(QPalette::Window, 0xFFf2f2f2);
            pal.setColor(QPalette::WindowText, 0xFF222222);
            pal.setColor(QPalette::Text, 0xFF222222);

            pal.setColor(QPalette::Base, 0xFFfefefe);
            pal.setColor(QPalette::AlternateBase, 0xFFeaeaea);

            pal.setColor(QPalette::NoRole, Qt::cyan);
        }

        {
            QPalette &pal = _theme->darkPalette;
            pal.setColor(QPalette::Button, Qt::yellow);
            pal.setColor(QPalette::ButtonText, Qt::white);
            pal.setColor(QPalette::Light, Qt::cyan);
            pal.setColor(QPalette::Midlight, Qt::darkCyan);
            pal.setColor(QPalette::Dark, 0xFF313131); // darker than button
            pal.setColor(QPalette::Mid, 0xFF383838); // between dark and button
            pal.setColor(QPalette::Shadow, 0xFF101010);


            //pal.setColor(QPalette::Window, 0xFF1d1d1d);
            pal.setColor(QPalette::Window, 0xFF202020);
            pal.setColor(QPalette::WindowText, 0xFFe4e4e5);
            pal.setColor(QPalette::Text, 0xFFe4e4e5);
            pal.setColor(QPalette::Disabled, QPalette::Text, 0xFF646465);

            pal.setColor(QPalette::Base, 0xFF282828);
            pal.setColor(QPalette::AlternateBase, 0xFF2f2f2f);

            pal.setColor(QPalette::Button, 0xFF434343);
            pal.setColor(QPalette::ButtonText, 0xFFeaebec);

            pal.setColor(QPalette::BrightText, 0xFFf1f1f2);

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
        colorNodeBase = 0xFFefefef;
        colorNodeViewBackground = 0xFFfafafa;
        _theme->palette = _theme->lightPalette;

    }
    else if (theme == "dark")
    {
        colorNodeBase = 0xFF202020;
        colorNodeViewBackground = 0xFF313131;
        _theme->palette = _theme->darkPalette;
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
    case ColorNodeViewBackground: return colorNodeViewBackground;
    default: break;
    }

    return Qt::magenta; // to easily spot unhandled values
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
#if 1
    QPalette pal = _theme->palette;

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
        widget->setAutoFillBackground(true);
    }
    else if (widget->objectName() == "zmClusterInfo")
    {
        // TODO(mpi) this is a hack to get same background color as other widgets in dock
        pal.setColor(QPalette::Window, _theme->palette.color(QPalette::Mid));
    }
    // Main window dock widget tabs use these
    // else if (qobject_cast<const QTabBar*>(widget))
    // {
    //     pal.setColor(QPalette::Window, _theme->palette.color(QPalette::Dark));
    // }
    // else if (qobject_cast<const QTreeView*>(widget))
    // {
    //     //pal.setColor(QPalette::Base, colorTableBase);
    //     //pal.setColor(QPalette::AlternateBase, colorTableAlternateBase);
    // }
    // else if (qobject_cast<const QTabBar*>(widget->parentWidget()))
    // {
    //     pal.setColor(QPalette::Window, Qt::red);
    //     pal.setColor(QPalette::Active, QPalette::Window, Qt::green);
    //     widget->setAutoFillBackground(true);
    // }
    // else if (qobject_cast<const QTabWidget*>(widget->parentWidget()))
    // {
    //     //pal.setColor(QPalette::Window, colorTabWidgetWindow);
    //     pal.setColor(QPalette::Window, Qt::cyan);
    //     //pal.setColor(QPalette::Window, _theme->palette.color(QPalette::Dark));
    //     widget->setAutoFillBackground(true);
    // }
    else
    {

    }

    ASuper::polish(widget);
    widget->setPalette(pal);
#endif
}

void AStyle::polish(QPalette &pal)
{
    ASuper::polish(pal);
}

void AStyle::unpolish(QWidget *widget)
{
#if 0
    if (qobject_cast<const QDockWidget*>(widget))
    {
        widget->setAutoFillBackground(false);
    }
    else if (qobject_cast<const QTabBar*>(widget->parentWidget()))
    {
        widget->setAutoFillBackground(false);
    }
    else if (qobject_cast<const QTabWidget*>(widget->parentWidget()))
    {
        widget->setAutoFillBackground(false);
    }
#endif

    //widget->setPalette(_theme->initialPalette);
    ASuper::unpolish(widget);
}

void AStyle::unpolish(QApplication *app)
{
    // app->setPalette(_theme->initialPalette);
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

    if (element == PE_FrameTabWidget)
    {
        painter->save();
        const QColor fillColor = _theme->palette.color(QPalette::Mid);
        painter->fillRect(option->rect.adjusted(0, 0, -1, -1), fillColor);
        painter->restore();
        return;
    }

    // if (option->type == QStyleOption::SO_Default)
    // {

    //     QStyleOption opt =  *option;
    //     opt.palette = _theme->darkPalette;
    //     ASuper::drawPrimitive(element, &opt, painter, widget);
    // }
    // else
    // if (element == PE_Widget)
    // {
    //     // QStyleOption opt =  *option;
    //     // opt.palette = _theme->darkPalette;
    //     // ASuper::drawPrimitive(element, &opt, painter, widget);
    // }
    // else if (element == PE_Frame && option->type == QStyleOption::SO_Frame)
    // {
    //     QStyleOptionFrame opt = *qstyleoption_cast<const QStyleOptionFrame*>(option);
    //     painter->setBrush(Qt::green);
    //     painter->drawRect(opt.rect);
    //     return;
    //     opt.palette = _theme->palette;
    //     qDebug() << widget->objectName() << element << " backgroundRole: " <<  widget->backgroundRole() << "shape" << opt.frameShape;
    //     ASuper::drawPrimitive(element, &opt, painter, widget);
    // }
    // else if (element == PE_PanelItemViewRow && option->type == QStyleOption::SO_ViewItem)
    // {
    //     QStyleOptionViewItem opt = *qstyleoption_cast<const QStyleOptionViewItem*>(option);
    //     opt.palette = _theme->palette;
    //     ASuper::drawPrimitive(element, &opt, painter, widget);
    // }
    // else if (element == PE_PanelItemViewItem && option->type == QStyleOption::SO_ViewItem)
    // {
    //     QStyleOptionViewItem opt = *qstyleoption_cast<const QStyleOptionViewItem*>(option);
    //     opt.palette = _theme->palette;
    //     ASuper::drawPrimitive(element, &opt, painter, widget);
    // }
    // else
    {
       // qDebug() << "elem: " << element << "type: " << option->type;
        ASuper::drawPrimitive(element, option, painter, widget);
    }
}

void AStyle::drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    if (element == CE_TabBarTabShape)
    {
        if (const QStyleOptionTab *tab = qstyleoption_cast<const QStyleOptionTab*>(option))
        {
            QStyleOptionTab opt1 = *tab;
            // QPalette::Button is used on QTabWidget tabs
            // Note(mpi): this also depends on QStyleOptionTab::HasFrame
            //opt1.palette.setColor(QPalette::Button, _theme->palette.color(QPalette::Mid));
            // QPalette::Window is used on tabs below QDockWidget
            //opt1.palette.setColor(QPalette::Window, _theme->palette.color(QPalette::Dark));
            //ASuper::drawControl(element, &opt1, painter, widget);

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

            // QColor tabFrameColor = tab->features & QStyleOptionTab::HasFrame ?
            //             d->tabFrameColor(option->palette) :
            //             option->palette.window().color();
            //QColor tabFrameColor = option->palette.window().color();

            //QLinearGradient fillGradient(rect.topLeft(), rect.bottomLeft());
            //QLinearGradient outlineGradient(rect.topLeft(), rect.bottomLeft());
            // const QColor outline = d->outline(option->palette);
            // const QColor outline = Qt::yellow;
            QColor fillColor;
            if (selected) {
                fillColor = _theme->palette.color(QPalette::Mid);
            //     fillGradient.setColorAt(0, tabFrameColor.lighter(104));
            //     fillGradient.setColorAt(1, tabFrameColor);
            //     outlineGradient.setColorAt(1, outline);
            //     painter->setPen(QPen(outlineGradient, 1));
            } else {
                fillColor = _theme->palette.color(QPalette::Dark);
            //     fillGradient.setColorAt(0, tabFrameColor.darker(108));
            //     fillGradient.setColorAt(0.85, tabFrameColor.darker(108));
            //     fillGradient.setColorAt(1, tabFrameColor.darker(116));
            //     painter->setPen(outline.lighter(110));
            }

            QRect drawRect = rect.adjusted(0, selected ? 0 : 2, 0, 3);
            painter->save();
            painter->setClipRect(rect.adjusted(-1, -1, 1, selected ? -2 : -3));
            //painter->setBrush(fillGradient);
            painter->setBrush(fillColor);
            painter->drawRoundedRect(drawRect.adjusted(0, 0, -1, -1), 2.0, 2.0);
            //painter->setBrush(Qt::NoBrush);
            //const QColor innerContrastLine(Qt::red);
            // painter->setPen(QFusionStylePrivate::innerContrastLine);
            //painter->drawRoundedRect(drawRect.adjusted(1, 1, -2, -1), 2.0, 2.0);
            painter->restore();

            // if (selected) {
            //     painter->fillRect(rect.left() + 1, rect.bottom() - 1, rect.width() - 2, rect.bottom() - 1, tabFrameColor);
            //     painter->fillRect(QRect(rect.bottomRight() + QPoint(-2, -1), QSize(1, 1)), innerContrastLine);
            //     painter->fillRect(QRect(rect.bottomLeft() + QPoint(0, -1), QSize(1, 1)), innerContrastLine);
            //     painter->fillRect(QRect(rect.bottomRight() + QPoint(-1, -1), QSize(1, 1)), innerContrastLine);
            // }
        }
        painter->restore();
        }
        return;
    }
    else if (element == CE_ToolBar)
    {
        // the style only draws the background with a solid color
        painter->setPen(Qt::NoPen);
        painter->setBrush(_theme->palette.dark());
        painter->drawRect(option->rect);
        return;
    }

    ASuper::drawControl(element, option, painter, widget);
}

int AStyle::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    return ASuper::pixelMetric(metric, option, widget);
}

QPalette AStyle::standardPalette() const
{
    return _theme->palette;
}
