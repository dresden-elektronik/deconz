/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QComboBox>
#include <QCheckBox>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QSignalMapper>
#include <QLineEdit>
#include <QTimer>
#include <QSlider>
#include <memory>

#include "deconz/aps.h"
#include "deconz/types.h"
#include "deconz/dbg_trace.h"
#include "zm_cluster_info.h"
#include "zm_command_info.h"
#include "ui_zm_command_info.h"
#include "zcl_private.h"

zmCommandInfo::zmCommandInfo(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::zmCommandInfo)
{
    ui->setupUi(this);
    m_vbox = new QVBoxLayout(this);
    m_execMapper = new QSignalMapper(this);

    m_commandTimeout = 10 * 1000;

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);

    connect(m_timer, SIGNAL(timeout()),
            this, SLOT(zclCommandTimeout()));

    connect(m_execMapper, SIGNAL(mapped(int)),
            this, SLOT(onExec(int)));
}

zmCommandInfo::~zmCommandInfo()
{
    delete ui;
}

void zmCommandInfo::setCluster(quint16 profileId, const deCONZ::ZclCluster &cluster, deCONZ::ZclClusterSide side)
{
    bool changed = false;

    if (m_profileId != profileId
     || m_cluster.id() != cluster.id()
     || m_cluster.isServer() != cluster.isServer())
    {
        changed = true;
    }

    m_timer->stop();
    m_profileId = profileId;
    m_side = side;
    m_cluster = cluster;

    if (changed)
    {
        if (m_cluster.isServer())
        {
            m_clusterOpposite = deCONZ::zclDataBase()->outCluster(m_profileId, m_cluster.oppositeId(), m_cluster.manufacturerCode());
        }
        else
        {
            m_clusterOpposite = deCONZ::zclDataBase()->inCluster(m_profileId, m_cluster.oppositeId(), m_cluster.manufacturerCode());
        }
    }

    setCommandState(0xFF, deCONZ::IdleState, QLatin1String(""));

    // remove widgets from layout
    if (changed)
    {
        while (m_vbox->count() > 0)
        {
            QLayoutItem *item = m_vbox->itemAt(0);
            if (item)
            {
                if (item->widget())
                {
                    item->widget()->hide();
                }

                m_vbox->removeItem(item);
            }
        }
    }

    // check if the widgets for this cluster already exist
    bool found = false;
    int idx = 0;
    for (CommandCacheIterator i = m_cache.begin(); i != m_cache.end(); ++i, ++idx)
    {
        if (i->profileId == profileId && i->clusterId == cluster.id() && i->side == side)
        {
            if (changed)
            {
                m_vbox->addWidget(i->widget);
            }
            i->widget->show();
            found = true;
            showCommandParameters(*i, false);
        }
        else
        {
            if (found)
            {
                // got all
                return;
            }
        }
    }

    if (found)
    {
        // done
        return;
    }

    // create new widgets
    std::vector<deCONZ::ZclCommand>::const_iterator i;
    for (i = m_cluster.commands().begin(); i != m_cluster.commands().end(); ++i)
    {
        CommandDescriptor descr;

        descr.profileId = profileId;
        descr.clusterId = cluster.id();
        descr.side = side;
        descr.widget = 0;
        descr.command = *i;

        if (descr.command.hasResponse())
        {
            std::vector<deCONZ::ZclCommand>::const_iterator j;
            for (j = m_clusterOpposite.commands().begin(); j != m_clusterOpposite.commands().end(); ++j)
            {
                if (i->responseId() == j->id())
                {
                    descr.responseCommand = *j;
                    break;
                }
            }
        }

        createCommandWidget(descr, false); // request widget

        if (descr.responseCommand.isValid())
        {
            createCommandWidget(descr, true); // response widget
        }

        if (descr.widget != 0)
        {
            m_cache.append(descr);
            m_vbox->addWidget(descr.widget);
            descr.widget->show();

//            if (descr.responseWidget != 0)
//            {
//                m_vbox->addWidget(descr.responseWidget);
//                descr.responseWidget->show();
//            }
        }
    }
}

/*!
    Handler then the user clicks the exec button.
 */
void zmCommandInfo::onExec(int commandId)
{
    CommandDescriptor *descriptor = 0;

    for (CommandCacheIterator i = m_cache.begin(); i != m_cache.end(); ++i)
    {
        if (i->profileId == m_profileId && i->clusterId == m_cluster.id() && i->side == m_side &&
            i->command.id() == commandId)
        {
            descriptor = &(*i);
            break;
        }
    }

    if (!descriptor)
    {
        DBG_Printf(DBG_INFO, "Command info, unknown command id: 0x%02X\n", static_cast<quint8>(commandId));
        return;
    }

    auto i = m_cluster.commands().begin();
    auto end = m_cluster.commands().end();

    for (; i != end; ++i)
    {
        deCONZ::ZclCommand &command = *i;

        if (command.id() == commandId)
        {
            uint k = 0; // attribute widget iter
            std::vector<deCONZ::ZclAttribute>::iterator j = i->parameters().begin();
            std::vector<deCONZ::ZclAttribute>::iterator jend = i->parameters().end();
            for (; j != jend; ++j, ++k)
            {
                if (k < descriptor->parameterAttributes.size())
                {
                    if (!getParameter(*j, descriptor->parameterAttributes[k]))
                    {
                        DBG_Printf(DBG_INFO, "Command info, failed to get parameter for 0x%04X, invalid user input\n", j->id());
                        return;
                    }
                }
                else
                {
                    DBG_Printf(DBG_INFO, "Command info, wrong payload count for 0x%04X\n", j->id());
                    return;
                }
            }

            m_commandId = commandId;
            if (deCONZ::clusterInfo()->zclCommandRequest(m_cluster, m_side, command))
            {
                if (m_cluster.isZcl())
                {
                    setCommandState(command.id(), deCONZ::BusyState, tr("executing ..."));
                    m_timer->start(m_commandTimeout);
                }
                else if (!m_cluster.isZcl() && command.hasResponse())
                {
                    setCommandState(command.id(), deCONZ::BusyState, tr("executing ..."));
                    m_timer->start(m_commandTimeout);
                }
            }
            else
            {
                DBG_Printf(DBG_INFO, "Command info, can't send ZCL command\n");
            }
            break;
        }
    }
}

void zmCommandInfo::zclCommandResponse(const deCONZ::ApsDataIndication &ind, const deCONZ::ZclFrame &zclFrame)
{
    if (m_clusterOpposite.id() != ind.clusterId())
    {
        return;
    }

    if (m_timer->isActive())
    {
        m_timer->stop();
    }

    if (m_cluster.isZcl())
    {
        // check for default response command
        // which must be a profile wide command
        if (zclFrame.isProfileWideCommand() &&
            zclFrame.commandId() == deCONZ::ZclDefaultResponseId &&
            zclFrame.payload().size() >= 2)
        {
            if ((quint8)zclFrame.payload()[0] == (quint8)m_commandId)
            {
                QString info;
                switch (zclFrame.payload()[1])
                {
                case deCONZ::ZclSuccessStatus:
                {
                    info = tr("success");
                }
                    break;

                default:
                {
                    // get human redable status string
                    deCONZ::Enumeration enumeration;
                    quint8 status = (quint8)zclFrame.payload()[1];
                    uint zclEnumeration = 0x00; // ZCL_Status, defined in ZCLDB/general.xml

                    if (deCONZ::zclDataBase()->getEnumeration(zclEnumeration, enumeration))
                    {
                        info = enumeration.getValueName(status);
                    }

                    if (info.isEmpty())
                    {
                        info = QString("status 0x%1").arg(status, 2, 16, QLatin1Char('0'));
                    }

                }
                    break;
                }

                setCommandState(m_commandId, deCONZ::IdleState, info, 0);
            }
        }
        else if (zclFrame.isClusterCommand())
        {
            // cluster specific response
            setCommandState(m_commandId, deCONZ::IdleState, "", &zclFrame);
        }
    }
    else
    {
        m_clusterOpposite.readCommand(ind);
        updateDescriptor();

        // non ZCL response
        setCommandState(m_commandId, deCONZ::IdleState, "", 0);
    }

    m_commandId = 0xFF;
}

void zmCommandInfo::zclCommandTimeout()
{
    setCommandState(m_commandId, deCONZ::TimeoutState, tr("timeout"));
    m_commandId = 0xFF;
}

/*!
    Create's a generic widget for a ZCL command.

    The widget contains name, description and parameter setup sub widgets.
 */
void zmCommandInfo::createCommandWidget(CommandDescriptor &descriptor, bool response)
{
    QWidget *w = nullptr;
    QVBoxLayout *lay = nullptr;
    const deCONZ::ZclCommand *cmd = nullptr;

    if (!response)
    {
        cmd = &descriptor.command;
        w = new QGroupBox(cmd->name());
        lay = new QVBoxLayout(w);
        descriptor.execButton = new QPushButton(tr("exec"));
        descriptor.statusLabel = new QLabel;
        descriptor.widget = w;
    }
    else
    {
        cmd = &descriptor.responseCommand;
        w = new QWidget;
        lay = new QVBoxLayout(w);
        descriptor.responseWidget = w;
        w->setContentsMargins(10, 5, 10, 5);
        // seperator
        QFrame *hline = new QFrame;
        hline->setFrameStyle(QFrame::HLine | QFrame::Plain);
        lay->addWidget(hline);
        // header
        QLabel *respHeader = new QLabel(cmd->name());
        respHeader->setAlignment(Qt::AlignHCenter);
        lay->addWidget(respHeader);

        descriptor.widget->layout()->addWidget(w);
    }

    // description
    if (!cmd->description().isEmpty())
    {
        QLabel *description = new QLabel(cmd->description());
        description->setWordWrap(true);
        lay->addWidget(description);
    }

    // payload
    QFormLayout *payLay = new QFormLayout;
    lay->addLayout(payLay);

    auto i = cmd->parameters().begin();
    auto end = cmd->parameters().end();

    for (; i != end; ++i)
    {
        QString tooltip;
        deCONZ::ZclDataType dataType = deCONZ::zclDataBase()->dataType(i->dataType());
        std::unique_ptr<QWidget> valueWidget;
        std::vector<QWidget*> payloadAttributes;

        switch (i->dataType())
        {
        case deCONZ::ZclBoolean:
        {
            QCheckBox *value = new QCheckBox;
            valueWidget.reset(value);
            value->setChecked(i->numericValue().u8 == 0x01);
            payloadAttributes.push_back(valueWidget.get());
        }
           break;

        // numeric inputs
        case deCONZ::Zcl8BitUint:
        case deCONZ::Zcl16BitUint:
        case deCONZ::Zcl24BitUint:
        case deCONZ::Zcl32BitUint:
        case deCONZ::Zcl40BitUint:
        case deCONZ::Zcl48BitUint:
        case deCONZ::Zcl56BitUint:
        case deCONZ::Zcl64BitUint:
        case deCONZ::Zcl8BitInt:
        case deCONZ::Zcl16BitInt:
        case deCONZ::Zcl24BitInt:
        case deCONZ::Zcl32BitInt:
        case deCONZ::Zcl40BitInt:
        case deCONZ::Zcl48BitInt:
        case deCONZ::Zcl56BitInt:
        case deCONZ::Zcl64BitInt:
        case deCONZ::ZclIeeeAddress:
        {
            if (dataType.isValid())
            {
                tooltip = dataType.name();
            }

            if (!i->description().isEmpty())
            {
                if (!tooltip.isEmpty())
                {
                    tooltip.append(", ");
                }
                tooltip.append(i->description());
            }

            if (i->formatHint() == deCONZ::ZclAttribute::DefaultFormat)
            {
                QLineEdit *value = new QLineEdit;
                valueWidget.reset(value);

                value->setInputMethodHints(Qt::ImhFormattedNumbersOnly);
                value->setText(i->toString(deCONZ::ZclAttribute::Prefix));
                payloadAttributes.push_back(valueWidget.get());
            }
            else if (i->formatHint() == deCONZ::ZclAttribute::SliderFormat)
            {
                QSlider *value = new QSlider;
                valueWidget.reset(value);
                value->setOrientation(Qt::Horizontal);
                value->setInputMethodHints(Qt::ImhFormattedNumbersOnly);

                if (i->dataType() < deCONZ::Zcl8BitInt)
                {
                    value->setValue(i->numericValue().u64);
                }
                else
                {
                    value->setValue(i->numericValue().s64);
                }

                if (i->rangeMin() != i->rangeMax())
                {
                    value->setMinimum(i->rangeMin());
                    value->setMaximum(i->rangeMax());
                }

                payloadAttributes.push_back(valueWidget.get());
            }

            if (valueWidget && !tooltip.isEmpty())
            {
                valueWidget->setToolTip(tooltip);
            }
        }
           break;

        case deCONZ::ZclOctedString:
        case deCONZ::ZclCharacterString:
        {
            QLineEdit *value = new QLineEdit;
            valueWidget.reset(value);
            value->setText(i->toString());
            payloadAttributes.push_back(valueWidget.get());
        }
           break;

        case deCONZ::Zcl8BitEnum:
        case deCONZ::Zcl16BitEnum:
        {
            if (!response)
            {
                QComboBox *combo = new QComboBox;
                valueWidget.reset(combo);
                auto names = i->valuesNames();
                auto values = i->valueNamePositions();

                DBG_Assert((int)names.size() == (int)values.size());
                if ((int)names.size() == (int)values.size())
                {
                    for (int i = 0; i < names.size(); i++)
                    {
                        combo->addItem(names[i], values[i]);
                    }
                }
            }
            else
            {
                QLabel *label = new QLabel;
                valueWidget.reset(label);
                label->setFrameStyle(QFrame::Sunken | QFrame::StyledPanel);
                label->setAlignment(Qt::AlignHCenter);
            }
            payloadAttributes.push_back(valueWidget.get());
        }
            break;

        case deCONZ::Zcl8BitBitMap:
        case deCONZ::Zcl16BitBitMap:
        case deCONZ::Zcl32BitBitMap:
        case deCONZ::Zcl40BitBitMap:
        case deCONZ::Zcl48BitBitMap:
        case deCONZ::Zcl56BitBitMap:
        case deCONZ::Zcl64BitBitMap:
        {
            const QStringList names = i->valuesNames();

            if (!names.isEmpty())
            {
                valueWidget = std::make_unique<QWidget>();
                valueWidget->setLayout(new QVBoxLayout);

                for (auto j = names.cbegin(); j != names.cend(); ++j)
                {
                    QCheckBox *checkBox = new QCheckBox(*j);
                    valueWidget->layout()->addWidget(checkBox);
                    payloadAttributes.push_back(checkBox);
                }
            }
        }
            break;

        default:
            // unsupported
            if (!response)
            {
                descriptor.execButton->setEnabled(false);
            }
            break;
        }

        if (valueWidget)
        {
            payLay->addRow(i->name(), valueWidget.get());
            valueWidget.release();
        }

        if (!response)
        {
            descriptor.parameterAttributes.push_back(payloadAttributes);
        }
        else
        {
            descriptor.responseParameterAttributes.push_back(payloadAttributes);
        }
    }

    if (!response)
    {
        // exec button
        QHBoxLayout *execLay = new QHBoxLayout;
        lay->addLayout(execLay);
        execLay->addStretch();
        execLay->addWidget(descriptor.statusLabel);
        execLay->addWidget(descriptor.execButton);
        connect(descriptor.execButton, SIGNAL(clicked()),
                m_execMapper, SLOT(map()));
        m_execMapper->setMapping(descriptor.execButton, (int)cmd->id());
    }
}

void zmCommandInfo::showCommandParameters(CommandDescriptor &descriptor, bool response)
{
    updateDescriptor();

    if (!response && descriptor.widget)
    {
        std::vector<deCONZ::ZclAttribute> &payload = descriptor.command.parameters();

        if (descriptor.parameterAttributes.size() != payload.size())
        {
            return;
        }

        for (uint i = 0; i < payload.size(); i++)
        {
            setParameter(payload[i], descriptor.parameterAttributes[i], false);
        }
    }

    //
    if (response && descriptor.responseWidget)
    {
        std::vector<deCONZ::ZclAttribute> &payload = descriptor.responseCommand.parameters();

        if (descriptor.responseParameterAttributes.size() != payload.size())
        {
            return;
        }

        for (uint i = 0; i < payload.size(); i++)
        {
            setParameter(payload[i], descriptor.responseParameterAttributes[i], true);
        }
    }
}

bool zmCommandInfo::setParameter(deCONZ::ZclAttribute &attr, std::vector<QWidget *> &widgets, bool response)
{
    deCONZ::ZclDataType dataType = deCONZ::zclDataBase()->dataType(attr.dataType());

    for (uint i = 0; i < widgets.size(); i++)
    {
        switch (attr.dataType())
        {
        case deCONZ::ZclBoolean:
        {
            QCheckBox *edit = qobject_cast<QCheckBox*>(widgets.front());
            if (edit && (response || !edit->hasFocus()))
            {
                edit->setChecked(attr.numericValue().u8 == 0x01);
                return true;
            }
        }
            break;

        case deCONZ::Zcl8BitUint:
        case deCONZ::Zcl16BitUint:
        case deCONZ::Zcl24BitUint:
        case deCONZ::Zcl32BitUint:
        case deCONZ::Zcl40BitUint:
        case deCONZ::Zcl48BitUint:
        case deCONZ::Zcl56BitUint:
        case deCONZ::Zcl64BitUint:
        case deCONZ::ZclIeeeAddress:
        {
            if (attr.formatHint() == deCONZ::ZclAttribute::DefaultFormat)
            {
                QLineEdit *edit = qobject_cast<QLineEdit*>(widgets[i]);
                if (edit && (response || !edit->hasFocus()))
                {
                    edit->setText(attr.toString(dataType, deCONZ::ZclAttribute::Prefix));
                    return true;
                }
            }
            else if (attr.formatHint() == deCONZ::ZclAttribute::SliderFormat)
            {
                QSlider *edit = qobject_cast<QSlider*>(widgets[i]);
                if (edit && (response || !edit->hasFocus()))
                {
                    edit->setValue(attr.numericValue().u64);
                    return true;
                }
            }
        }
            break;

        case deCONZ::Zcl8BitInt:
        case deCONZ::Zcl16BitInt:
        case deCONZ::Zcl24BitInt:
        case deCONZ::Zcl32BitInt:
        case deCONZ::Zcl40BitInt:
        case deCONZ::Zcl48BitInt:
        case deCONZ::Zcl56BitInt:
        case deCONZ::Zcl64BitInt:
        {
            if (attr.formatHint() == deCONZ::ZclAttribute::DefaultFormat)
            {
                QLineEdit *edit = qobject_cast<QLineEdit*>(widgets[i]);
                if (edit && (response || !edit->hasFocus()))
                {
                    edit->setText(attr.toString(dataType, deCONZ::ZclAttribute::Prefix));
                    return true;
                }
            }
            else if (attr.formatHint() == deCONZ::ZclAttribute::SliderFormat)
            {
                QSlider *edit = qobject_cast<QSlider*>(widgets[i]);
                if (edit && (response || !edit->hasFocus()))
                {
                    edit->setValue(attr.numericValue().s64);
                    return true;
                }
            }
        }
            break;

        case deCONZ::Zcl8BitBitMap:
        case deCONZ::Zcl16BitBitMap:
        case deCONZ::Zcl24BitBitMap:
        case deCONZ::Zcl32BitBitMap:
        case deCONZ::Zcl40BitBitMap:
        case deCONZ::Zcl48BitBitMap:
        case deCONZ::Zcl56BitBitMap:
        case deCONZ::Zcl64BitBitMap:
        {
            const auto &bits = attr.valueNamePositions();

            if (widgets.size() == bits.size())
            {
                for (uint i = 0; i < bits.size(); i++)
                {
                    QCheckBox *checkbox = qobject_cast<QCheckBox*>(widgets[i]);
                    if (!checkbox)
                    {
                        DBG_Printf(DBG_INFO, "Command info, no checkboxes for attribute 0x%04X\n", attr.id());
                        return false;
                    }

                    if (response || !checkbox->hasFocus())
                    {
                        checkbox->setChecked(attr.bit(bits[i]));
                    }
                }

                return true;
            }
            else
            {
                DBG_Printf(DBG_INFO, "Command info, widgets.size: %u != bits.size: %u\n", (unsigned)widgets.size(), (unsigned)bits.size());
            }
        }
            break;

        case deCONZ::Zcl8BitEnum:
        case deCONZ::Zcl16BitEnum:
        {
            QLabel *label;
            QComboBox *combo = qobject_cast<QComboBox*>(widgets.front());
            if (combo && (response || !combo->hasFocus()))
            {
                for (int i = 0; i < combo->count(); i++)
                {
                    if (combo->itemData(i).toInt() == (int)attr.enumerator())
                    {
                        combo->setCurrentIndex(i);
                        return true;
                    }
                }
            }
            else if (attr.enumerationId() != 0xFF)
            {
                label = qobject_cast<QLabel*>(widgets.front());
                if (label)
                {
                    deCONZ::Enumeration enumeration;
                    if (!deCONZ::zclDataBase()->getEnumeration(attr.enumerationId(), enumeration))
                    {
                        return false;
                    }

                    label->setText(enumeration.getValueName(attr.enumerator()));
                    return true;
                }
            }
            else
            {
                label = qobject_cast<QLabel*>(widgets.front());
                if (label)
                {
                    QString name = attr.valueNameAt(attr.enumerator());
                    label->setText(name);
                    return true;
                }
            }
        }
            break;

        case deCONZ::ZclOctedString:
        case deCONZ::ZclCharacterString:
        {
            QLineEdit *edit = qobject_cast<QLineEdit*>(widgets[i]);
            if (edit && (response || !edit->hasFocus()))
            {
                edit->setText(attr.toString(dataType));
                return true;
            }
        }
            break;

        default:
            break;
        }
    }

    return false;
}

bool zmCommandInfo::getParameter(deCONZ::ZclAttribute &attr, std::vector<QWidget *> &widgets)
{
    for (uint i = 0; i < widgets.size(); i++)
    {
        switch (attr.dataType())
        {
        case deCONZ::ZclBoolean:
        {
            QCheckBox *edit = qobject_cast<QCheckBox*>(widgets.front());
            if (edit)
            {
                attr.setValue(edit->isChecked());
                return true;
            }
        }
            break;

        case deCONZ::Zcl8BitUint:
        case deCONZ::Zcl16BitUint:
        case deCONZ::Zcl24BitUint:
        case deCONZ::Zcl32BitUint:
        case deCONZ::Zcl40BitUint:
        case deCONZ::Zcl48BitUint:
        case deCONZ::Zcl56BitUint:
        case deCONZ::Zcl64BitUint:
        case deCONZ::ZclIeeeAddress:
        {
            if (attr.formatHint() == deCONZ::ZclAttribute::DefaultFormat)
            {
                QLineEdit *edit = qobject_cast<QLineEdit*>(widgets[i]);
                if (edit)
                {
                    bool ok;
                    quint64 value = edit->text().toULongLong(&ok, attr.numericBase());
                    if (ok)
                    {
                        attr.setValue(value);
                        return true;
                    }
                }
            }
            else if (attr.formatHint() == deCONZ::ZclAttribute::SliderFormat)
            {
                QSlider *edit = qobject_cast<QSlider*>(widgets[i]);
                if (edit)
                {
                    quint64 value = edit->value();
                    attr.setValue(value);
                    return true;
                }
            }
        }
            break;

        case deCONZ::Zcl8BitInt:
        case deCONZ::Zcl16BitInt:
        case deCONZ::Zcl24BitInt:
        case deCONZ::Zcl32BitInt:
        case deCONZ::Zcl40BitInt:
        case deCONZ::Zcl48BitInt:
        case deCONZ::Zcl56BitInt:
        case deCONZ::Zcl64BitInt:
        {
            if (attr.formatHint() == deCONZ::ZclAttribute::DefaultFormat)
            {
                QLineEdit *edit = qobject_cast<QLineEdit*>(widgets[i]);
                if (edit)
                {
                    bool ok;
                    qint64 value = edit->text().toLongLong(&ok, attr.numericBase());
                    if (ok)
                    {
                        attr.setValue(value);
                        return true;
                    }
                }
            }
            else if (attr.formatHint() == deCONZ::ZclAttribute::SliderFormat)
            {
                QSlider *edit = qobject_cast<QSlider*>(widgets[i]);
                if (edit)
                {
                    quint64 value = edit->value();
                    attr.setValue(value);
                    return true;
                }
            }
        }
            break;

        case deCONZ::Zcl8BitBitMap:
        case deCONZ::Zcl16BitBitMap:
        case deCONZ::Zcl24BitBitMap:
        case deCONZ::Zcl32BitBitMap:
        case deCONZ::Zcl40BitBitMap:
        case deCONZ::Zcl48BitBitMap:
        case deCONZ::Zcl56BitBitMap:
        case deCONZ::Zcl64BitBitMap:
        {
            std::vector<int> bits = attr.valueNamePositions();

            if (widgets.size() == bits.size())
            {
                for (uint i = 0; i < bits.size(); i++)
                {
                    QCheckBox *checkbox = qobject_cast<QCheckBox*>(widgets[i]);
                    if (!checkbox)
                    {
                        DBG_Printf(DBG_INFO, "Command info, no checkboxes for attribute 0x%04X\n", attr.id());
                        return false;
                    }

                    attr.setBit(bits[i], checkbox->isChecked());
                }

                return true;
            }
        }
            break;

        case deCONZ::Zcl8BitEnum:
        case deCONZ::Zcl16BitEnum:
        {
            QComboBox *combo = qobject_cast<QComboBox*>(widgets.front());
            if (combo)
            {
                int enumValue = combo->currentData().toInt();
                attr.setEnumerator(enumValue);
                return true;
            }
        }
            break;

        case deCONZ::ZclOctedString:
        case deCONZ::ZclCharacterString:
        {
            QLineEdit *edit = qobject_cast<QLineEdit*>(widgets[i]);
            if (edit)
            {
                attr.setValue(edit->text());
                return true;
            }
        }
            break;

        default:
            break;
        }
    }

    return false;
}

void zmCommandInfo::setCommandState(int commandId, deCONZ::CommonState state, const QString &info, const deCONZ::ZclFrame *zclFrame)
{
    for (CommandCacheIterator i = m_cache.begin(); i != m_cache.end(); ++i)
    {
        switch (state)
        {
        case deCONZ::BusyState:
            i->execButton->setEnabled(false);
            break;

        case deCONZ::FailureState:
        case deCONZ::TimeoutState:
        case deCONZ::IdleState:
        default:
            i->execButton->setEnabled(true);
            break;
        }

        if (i->widget->isVisible())
        {
            clearData(*i);

            if (i->command.id() == commandId)
            {
                i->statusLabel->setText(info);

                // read payload into responseCommand attributes
                if (m_cluster.isZcl() && zclFrame)
                {
                    m_cluster.readCommand(*zclFrame);
                    m_clusterOpposite.readCommand(*zclFrame);
//                    updateDescriptor();
                    showCommandParameters(*i, true);

//                    if (zclFrame && i->responseCommand.isValid()
//                    && (i->responseCommand.id() == zclFrame->commandId()))
//                    {
//                        QDataStream stream(zclFrame->payload());
//                        stream.setByteOrder(QDataStream::LittleEndian);

//                        if (!i->responseCommand.readFromStream(stream))
//                        {
//                            qDebug() << Q_FUNC_INFO << "can't read command response";
//                        }

//                        showCommandParameters(*i, true);
//                    }
                }
                else
                {
                    showCommandParameters(*i, true);
                }
            }
        }
    }
}

void zmCommandInfo::clearData(zmCommandInfo::CommandDescriptor &descriptor)
{
    if (descriptor.statusLabel)
    {
        descriptor.statusLabel->clear();
    }

    for (uint i = 0; i < descriptor.responseParameterAttributes.size(); i++)
    {
        std::vector<QWidget*>::iterator w = descriptor.responseParameterAttributes[i].begin();
        std::vector<QWidget*>::iterator wend = descriptor.responseParameterAttributes[i].end();

        for (; w != wend; ++w)
        {
            QLabel *edit = qobject_cast<QLabel*>(*w);

            if (edit)
            {
                if (!edit->text().isEmpty())
                {
                    DBG_Printf(DBG_INFO, "Command info, clear data: %s\n", qPrintable(edit->text()));
                }
                edit->clear();
            }
        }
    }
}

/*!
    Update cached commands from cluster data.
 */
void zmCommandInfo::updateDescriptor()
{
    for (CommandCacheIterator i = m_cache.begin(); i != m_cache.end(); ++i)
    {
        if (i->profileId == m_profileId && i->clusterId == m_cluster.id())
        {
            for (uint j = 0; j < m_cluster.commands().size(); j++)
            {
                deCONZ::ZclCommand &ccmd = m_cluster.commands()[j];
                if (i->command.id() == ccmd.id() &&
                   (i->command.directionReceived() == ccmd.directionReceived()))
                {
                    i->command = ccmd;
                    break;
                }
            }


            if (!m_cluster.isZcl())
            {
                for (uint j = 0; j < m_clusterOpposite.commands().size(); j++)
                {
                    deCONZ::ZclCommand &ccmd = m_clusterOpposite.commands()[j];
                    if (i->responseCommand.id() == ccmd.id() &&
                       (i->responseCommand.directionReceived() == ccmd.directionReceived()))
                    {
                        i->responseCommand = ccmd;
                        break;
                    }
                }
            }
        }

        if (i->profileId == m_profileId && i->clusterId == m_clusterOpposite.id())
        {
            for (uint j = 0; j < m_clusterOpposite.commands().size(); j++)
            {
                deCONZ::ZclCommand &ccmd = m_clusterOpposite.commands()[j];
                if (i->responseCommand.id() == ccmd.id() &&
                   (i->responseCommand.directionReceived() == ccmd.directionReceived()))
                {
                    i->responseCommand = ccmd;
                    break;
                }
            }
        }
    }
}

void zmCommandInfo::zclCommandRequestError()
{
    setCommandState(m_commandId, deCONZ::FailureState, tr("sending failed"));
    m_commandId = 0xFF;
}

void zmCommandInfo::zclAllRequestsConfirmed()
{
    if (m_timer->isActive())
    {
        m_timer->stop();
    }

    for (CommandCacheIterator i = m_cache.begin(); i != m_cache.end(); ++i)
    {
        if (!i->execButton->isEnabled())
        {
            i->execButton->setEnabled(true);
            i->statusLabel->clear();
        }
    }
}
