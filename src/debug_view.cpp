/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QCheckBox>
#include <QTimer>
#include <QThread>
#include "deconz/dbg_trace.h"
#include "debug_view.h"
#include "ui_debug_view.h"

DebugView *_dbgView = nullptr;

static void dbgCallback(int level, const char *msg)
{
    if (_dbgView)
    {
        _dbgView->log(level, msg);
    }
}

DebugView::DebugView(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DebugView)
{
    Q_ASSERT(_dbgView == nullptr);
    _dbgView = this;
    ui->setupUi(this);

    const std::vector<int> levels = { DBG_INFO, DBG_INFO_L2, DBG_ERROR, DBG_ERROR_L2,
                                      DBG_DDF, DBG_DEV, DBG_JS, DBG_APS, DBG_APS_L2, DBG_ZGP,
                                      DBG_ZDP, DBG_ZCL, DBG_ZCLDB, DBG_IAS, DBG_OTA,
                                      DBG_HTTP, DBG_TLINK, DBG_ROUTING, DBG_MEASURE
#ifdef QT_DEBUG
        , DBG_PROT, DBG_VFS
#endif
    };

    for (const auto level : levels)
    {
        char buf[32];
        if (DBG_StringFromItem(level, buf, sizeof(buf)) < 0)
        {
            continue;
        }

        QCheckBox *chk = new QCheckBox(QLatin1String(buf), ui->dbgItems);
        chk->setProperty("item", level);
        ui->dbgItems->layout()->addWidget(chk);
        connect(chk, &QCheckBox::stateChanged, this, &DebugView::checkboxStateChanged);

        chk->setChecked(DBG_IsEnabled(level));
    }

    QSpacerItem *spacer = new QSpacerItem(24,24, QSizePolicy::Minimum, QSizePolicy::Expanding);
    ui->dbgItems->layout()->addItem(spacer);
    ui->log->setMaximumBlockCount(5000);

    QTimer::singleShot(20, this, &DebugView::init);
}

DebugView::~DebugView()
{
    _dbgView = nullptr;
    delete ui;
}

void DebugView::log(int level, const char *msg)
{
    Q_UNUSED(level)

    if (_dbgView->thread() == QThread::currentThread())
    {
#ifdef ARCH_ARM
        if (!isVisible())
        {
            return;
        }
#endif

        QString text(msg);
        while (text.endsWith('\n'))
        {
            text.chop(1);
        }
        ui->log->appendPlainText(text);
    }
    else
    {
        // discard message from other threads, todo
    }
}

void DebugView::checkboxStateChanged(int state)
{
    auto *chk = qobject_cast<QCheckBox*>(sender());
    Q_ASSERT(chk);

    const auto item = chk->property("item");

    Q_ASSERT(!item.isNull());
    Q_ASSERT(item.type() == QVariant::Int);

    if (state == Qt::Checked)
    {
        DBG_Enable(item.toInt());
    }
    else
    {
        DBG_Disable(item.toInt());
    }
}

void DebugView::init()
{
    // QApplication needs to be running
    DBG_RegisterCallback(dbgCallback);
}
