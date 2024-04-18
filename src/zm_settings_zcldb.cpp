/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QStringListModel>
#include <QFile>
#include <QFileDialog>
#include <QTextStream>
#include "zm_settings_zcldb.h"
#include "ui_zm_settings_zcldb.h"
#include "deconz/types.h"
#include "deconz/dbg_trace.h"
#include "deconz/util.h"
#include "zm_global.h"

class MyStringListModel : public QStringListModel
{
public:
    MyStringListModel(QObject *parent = 0) :
        QStringListModel(parent)
    {

    }

    Qt::ItemFlags flags(const QModelIndex&index) const
    {
        Qt::ItemFlags flags = QStringListModel::flags(index);

        if (index.isValid())
            flags =  Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsEnabled;
        else
            flags =  Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled  | Qt::ItemIsEnabled;

        return flags;
    }
};

zmSettingsZcldb::zmSettingsZcldb(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::zmSettingsZcldb)
{
    ui->setupUi(this);
    m_model = new MyStringListModel(this);
    ui->listView->setModel(m_model);
    ui->listView->setDragEnabled(true);
    ui->listView->setAcceptDrops(true);
    ui->listView->setDropIndicatorShown(true);
    ui->listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->listView->setDragDropMode(QAbstractItemView::InternalMove);

    /*
      // You still want the root index to accepts drops, just not the items so try:
    Qt::ItemFlags MyListModel::flags(const QModelIndex&index) const
    {
        Qt::ItemFlags flags; //= QStringListModel::flags(index);

        if (index.isValid())
            flags =  Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsEnabled;
        else
            flags =  Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled  | Qt::ItemIsEnabled;

        return flags;
    }

    // This will work if the root index is invalid (as it usually is).
    // However, if you use setRootIndex you may have to compare against that index instead.
    */

    connect(ui->listView, SIGNAL(clicked(QModelIndex)),
            this, SLOT(itemClicked(QModelIndex)));

    ui->listView->setMovement(QListView::Snap);

    connect(ui->addButton, SIGNAL(clicked()),
            this, SLOT(addItem()));

    connect(ui->removeButton, SIGNAL(clicked()),
            this, SLOT(removeItem()));

    m_selectedRow = -1;
    m_dataChanged = false;

    load();
}

zmSettingsZcldb::~zmSettingsZcldb()
{
    delete ui;
}

void zmSettingsZcldb::save()
{
    if (m_dataChanged)
    {
        m_dataChanged = false;

        QStringList ls = m_model->stringList();
        QString path = deCONZ::getStorageLocation(deCONZ::ZcldbLocation);

        QFile file(path);

        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            if (!ls.isEmpty())
            {
                QTextStream stream(&file);

                while (!ls.isEmpty())
                {
                    QString path = ls.takeFirst();
                    stream << path << "\r\n";
                }
            }
            file.close();
        }
        else
        {
            DBG_Printf(DBG_ERROR, "failed to open %s: %s\n", qPrintable(path), qPrintable(file.errorString()));
        }
    }
}

void zmSettingsZcldb::load()
{
    bool needSave = false;
    QStringList ls;
    QString path = deCONZ::getStorageLocation(deCONZ::ZcldbLocation);

    QFile file(path);

    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        while (!file.atEnd())
        {
            QByteArray line = file.readLine();

            if (line.contains(".xml"))
            {
                ls.append(line.trimmed());
            }
        }
    }
    else
    {
        DBG_Printf(DBG_ERROR, "failed to open %s: %s\n", qPrintable(path), qPrintable(file.errorString()));
    }

    if (ls.isEmpty())
    {
#ifdef Q_OS_UNIX // FIXME: readlink(/proc/self/exe)
        // append default.xml if not present
        QDir dir(qApp->applicationDirPath());
        dir.cdUp();
        dir.cd("share/deCONZ/zcl");
        QString gen(dir.absolutePath() + "/general.xml");
        if (QFile::exists(gen))
        {
            ls.append(gen);
            needSave = true;
        }
        else
        {
            DBG_Printf(DBG_INFO, "ZCLDB File %s/%s not found\n", qPrintable(dir.absolutePath()), qPrintable(gen));
        }
#endif
    }

    m_model->setStringList(ls);

    if (needSave)
    {
        if (file.isOpen())
        {
            file.close();
        }
        save();
    }
}

void zmSettingsZcldb::addItem()
{
    if (m_lastAddPath.isEmpty())
    {
        m_lastAddPath = deCONZ::getStorageLocation(deCONZ::HomeLocation);
    }

    QString fileName = QFileDialog::getOpenFileName(this,
         tr("Open ZCLDB File"), m_lastAddPath,
                tr("XML Files (*.xml)"));

    if (!fileName.isEmpty())
    {
        QStringList ls = m_model->stringList();
        m_lastAddPath = fileName;

        if (!ls.contains(fileName))
        {
            ls.append(fileName);
            m_model->setStringList(ls);
            m_dataChanged = true;
            emit dataChanged();
        }
    }
}

void zmSettingsZcldb::removeItem()
{
    if (m_selectedRow != -1)
    {
        m_model->removeRow(m_selectedRow);
        m_selectedRow = -1;
        m_dataChanged = true;
        emit dataChanged();
    }
}

void zmSettingsZcldb::itemClicked(const QModelIndex &index)
{
    if (index.isValid())
    {
        m_selectedRow = index.row();
    }
    else
    {
        m_selectedRow = -1;
    }
}
