/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QDrag>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QTimer>
#include <QMouseEvent>
#include <QMimeData>
#include <QUrl>
#include <QUrlQuery>
#include <QPainter>

#include "zm_attribute_info.h"
#include "ui_zm_attribute_info.h"
#include "zcl_private.h"

namespace
{
    const int MaxTimeout = 60 * 1000;
}

zmAttributeInfo::zmAttributeInfo(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::zmAttributeInfo),
    m_state(Idle)
{
    ui->setupUi(this);
    setProperty("theme.bgrole", QPalette::Mid);

    connect(ui->writeButton, SIGNAL(clicked()),
            this, SLOT(write()));

    connect(ui->readButton, SIGNAL(clicked()),
            this, SLOT(read()));

    connect(ui->closeButton, SIGNAL(clicked()),
            this, SLOT(reject()));

    m_timer = new QTimer(this);
    m_timer->setInterval(MaxTimeout);
    m_timer->setSingleShot(true);

    connect(m_timer, SIGNAL(timeout()),
            this, SLOT(timeout()));

    connect(ui->readReportConfButton, SIGNAL(clicked()),
            this, SLOT(readReportConfiguration()));

    connect(ui->writeReportConfButton, SIGNAL(clicked()),
            this, SLOT(writeReportConfiguration()));

    ui->status->clear();
    ui->reportingStatus->clear();
}

zmAttributeInfo::~zmAttributeInfo()
{
    delete ui;
}

void zmAttributeInfo::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_startDragPos = event->pos();
    }
}

void zmAttributeInfo::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;

    if ((event->pos() - m_startDragPos).manhattanLength() < QApplication::startDragDistance())
        return;

    QDrag *drag = new QDrag(this);
    QMimeData *mimeData = new QMimeData;

    QString attrIdString = QString("0x%1").arg(m_attribute.id(), 4, 16, QLatin1Char('0'));

    auto fm = fontMetrics();
    int w = fm.boundingRect(m_attribute.name() + attrIdString).width() + fm.xHeight() * 2;
    int h = fm.height() + 8;
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);

    QPainter p(&pm);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::white);
    p.setPen(QColor(64,64,64));
    p.drawRoundedRect(QRect(0, 0, w, h), 4, 4);

    const QColor colorAttr(171, 64, 18);
    p.setPen(colorAttr);
    p.drawText(QRect(4, 0, w, h), Qt::AlignLeft | Qt::AlignVCenter, attrIdString);

    p.setPen(Qt::black);
    p.drawText(QRect(0, 0, w - fm.xHeight(), h), Qt::AlignRight | Qt::AlignVCenter, m_attribute.name());
    drag->setPixmap(pm);

    QUrl url;
    QUrlQuery urlQuery;
    url.setScheme(QLatin1String("zclattr"));
    url.setPath("attr");
    urlQuery.addQueryItem("ep", "0x" + QString::number(m_endpoint, 16));
    urlQuery.addQueryItem("cid", "0x" + QString::number(m_clusterId, 16));
    urlQuery.addQueryItem("cs", (m_clusterSide == deCONZ::ClientCluster ? "c" : "s"));
    urlQuery.addQueryItem("mf", "0x" + QString::number(m_attribute.manufacturerCode(), 16));
    urlQuery.addQueryItem("a", "0x" + QString::number(m_attribute.id(), 16));
    urlQuery.addQueryItem("dt", "0x" + QString::number(m_attribute.dataType(), 16));
    urlQuery.addQueryItem("val", m_attribute.toString());

    if (ui->reportingWidget->isEnabled() &&
        !ui->minReportIntervalLineEdit->text().isEmpty() &&
        !ui->maxReportIntervalLineEdit->text().isEmpty())
    {
        urlQuery.addQueryItem("rmin", ui->minReportIntervalLineEdit->text());
        urlQuery.addQueryItem("rmax", ui->maxReportIntervalLineEdit->text());

        if (ui->reportableChangeLabel->isEnabled() && !ui->reportableChangeLineEdit->text().isEmpty())
        {
            urlQuery.addQueryItem("rchange", ui->reportableChangeLineEdit->text());
        }
    }

    deCONZ::ZclDataType type = deCONZ::zclDataBase()->dataType(m_attribute.dataType());

    if (type.isValid())
    {
        if (type.isAnalog())
        {
            urlQuery.addQueryItem("t", "A");
        }
        else
        {
            urlQuery.addQueryItem("t", "D");
        }
    }

    url.setQuery(urlQuery.toString());

    mimeData->setUrls({url});
    drag->setMimeData(mimeData);

    /*Qt::DropAction dropAction = */
    drag->exec(Qt::CopyAction);
}

void zmAttributeInfo::stateCheck()
{
    switch (m_state)
    {
    case Timeout:
        m_state = Idle; // fall through

    case Idle:
        if (m_attribute.isReadonly())
        {
            ui->writeButton->setEnabled(false);
        }
        else
        {
            ui->writeButton->setEnabled(true);
        }

        if (m_attribute.isWriteonly())
        {
            //ui->writeButton->setEnabled(false);
            ui->readButton->setEnabled(false);
        }
        else
        {
            ui->readButton->setEnabled(true);
        }

        ui->readReportConfButton->setEnabled(true);
        ui->writeReportConfButton->setEnabled(true);
        break;

    case ReadData:
    case WriteData:
        ui->reportingStatus->clear();
        ui->writeButton->setEnabled(false);
        ui->readButton->setEnabled(false);
        ui->readReportConfButton->setEnabled(false);
        ui->writeReportConfButton->setEnabled(false);
        break;

    case ReadConfig:
    case WriteConfig:
        ui->status->clear();
        ui->writeButton->setEnabled(false);
        ui->readButton->setEnabled(false);
        ui->readReportConfButton->setEnabled(false);
        ui->writeReportConfButton->setEnabled(false);
        break;

    default:
        break;
    }
}

void zmAttributeInfo::write()
{
    if (m_edits.isEmpty())
    {
        return;
    }

    bool ok = false;

    switch (m_attribute.dataType())
    {
    case deCONZ::ZclBoolean:
        ok = getBooleanInput();
        break;

    case deCONZ::Zcl8BitBitMap:
    case deCONZ::Zcl16BitBitMap:
    case deCONZ::Zcl24BitBitMap:
    case deCONZ::Zcl32BitBitMap:
    case deCONZ::Zcl40BitBitMap:
    case deCONZ::Zcl48BitBitMap:
    case deCONZ::Zcl56BitBitMap:
    case deCONZ::Zcl64BitBitMap:
        ok = getBitmapInput();
        break;

    case deCONZ::Zcl8BitEnum:
    case deCONZ::Zcl16BitEnum:
        ok = getEnumInput();
        break;

    case deCONZ::Zcl8BitData:
    case deCONZ::Zcl16BitData:
    case deCONZ::Zcl24BitData:
    case deCONZ::Zcl32BitData:
    case deCONZ::Zcl40BitData:
    case deCONZ::Zcl48BitData:
    case deCONZ::Zcl56BitData:
    case deCONZ::Zcl64BitData:

    case deCONZ::Zcl8BitInt:
    case deCONZ::Zcl16BitInt:
    case deCONZ::Zcl24BitInt:
    case deCONZ::Zcl32BitInt:
    case deCONZ::Zcl40BitInt:
    case deCONZ::Zcl48BitInt:
    case deCONZ::Zcl56BitInt:
    case deCONZ::Zcl64BitInt:

    case deCONZ::Zcl8BitUint:
    case deCONZ::Zcl16BitUint:
    case deCONZ::Zcl24BitUint:
    case deCONZ::Zcl32BitUint:
    case deCONZ::Zcl40BitUint:
    case deCONZ::Zcl48BitUint:
    case deCONZ::Zcl56BitUint:
    case deCONZ::Zcl64BitUint:
    case deCONZ::ZclIeeeAddress:
    case deCONZ::Zcl128BitSecurityKey:
    case deCONZ::ZclSingleFloat:
    case deCONZ::ZclOctedString:
        ok = getNumericInput();
        break;

    default:
        ok = false;
        break;
    }

    if (ok)
    {
        m_state = WriteData;
        emit zclWriteAttribute(m_attribute);
        m_timer->start();
        ui->status->setText(tr("writing ..."));
    }
    else
    {
        ui->status->setText(tr("invalid data"));
    }

    stateCheck();
}

void zmAttributeInfo::read()
{
    m_state = ReadData;
    emit zclReadAttribute(m_attribute);
    m_timer->start();
    ui->status->setText(tr("reading ..."));
    stateCheck();
}

void zmAttributeInfo::writeAttributeResponse(const deCONZ::ZclFrame &zclFrame)
{
    if (zclFrame.commandId() == deCONZ::ZclDefaultResponseId)
    {
        if (zclFrame.defaultResponseCommandId() == deCONZ::ZclWriteAttributesId)
        {
            if (zclFrame.defaultResponseStatus() != deCONZ::ZclSuccessStatus)
            {
                failedWithDefaultResponse(zclFrame);
                return;
            }
        }
    }

    if (zclFrame.commandId() != deCONZ::ZclWriteAttributesResponseId)
    {
        return;
    }

    quint8 status;
    QDataStream stream(zclFrame.payload());
    stream.setByteOrder(QDataStream::LittleEndian);

    stream >> status;

    if (status == deCONZ::ZclSuccessStatus)
    {
        ui->status->setText(tr("writing done"));
    }
    else
    {
        ui->status->setText(tr("writing failed"));
    }

    m_state = Idle;

}

void zmAttributeInfo::readAttributeResponse(const deCONZ::ZclFrame &zclFrame)
{

    if (zclFrame.commandId() == deCONZ::ZclDefaultResponseId)
    {
        if (zclFrame.defaultResponseCommandId() == deCONZ::ZclReadAttributesId)
        {
            if (zclFrame.defaultResponseStatus() != deCONZ::ZclSuccessStatus)
            {
                failedWithDefaultResponse(zclFrame);
                return;
            }
        }
    }

    if (zclFrame.commandId() != deCONZ::ZclReadAttributesResponseId)
    {
        return;
    }

    quint8 status;
    deCONZ::ZclDataTypeId_t dataType;
    deCONZ::ZclAttributeId_t attrId;

    QDataStream stream(zclFrame.payload());
    stream.setByteOrder(QDataStream::LittleEndian);

    stream >> attrId;
    stream >> status;

    if (attrId == m_attribute.id_t())
    {
        if (status == deCONZ::ZclSuccessStatus)
        {
            stream >> dataType;
            if (dataType == m_attribute.dataType_t())
            {
                //m_attribute.setDataType(dataType);
                m_attribute.readFromStream(stream);
                m_attribute.setManufacturerCode(zclFrame.manufacturerCode());
                updateEdit();
                ui->status->setText(tr("reading done"));
            }
            else
            {
                ui->status->setText(tr("got wrong data type"));
            }
        }
        else
        {
            switch (status)
            {
            case deCONZ::ZclUnsupportedAttributeStatus:
            case deCONZ::ZclClusterNotSupportedErrorStatus:
                ui->status->setText(tr("unsupported attribute"));
                break;

            default:
                ui->status->setText(tr("reading failed"));
                break;
            }

        }

    }

    m_state = Idle;
}

void zmAttributeInfo::timeout()
{
    switch (m_state)
    {
    case WriteData:
        ui->status->setText(tr("writing failed"));
        break;

    case ReadData:
        ui->status->setText(tr("Reading failed"));
        break;

    case WriteConfig:
        ui->reportingStatus->setText(tr("writing config failed"));
        break;

    case ReadConfig:
        ui->reportingStatus->setText(tr("Reading config failed"));
        break;

    default:
        break;
    }

    m_state = Timeout;
    stateCheck();
}

void zmAttributeInfo::failedWithDefaultResponse(const deCONZ::ZclFrame &zclFrame)
{
    QString status;
    deCONZ::Enumeration zclStatusEnum;

    if (deCONZ::zclDataBase()->getEnumeration(ZCL_ENUM, zclStatusEnum))
    {
        status = zclStatusEnum.getValueName((quint8)zclFrame.defaultResponseStatus());
    }

    if (status.isEmpty())
    {
        status = "0x" + QString("%1").arg(zclFrame.defaultResponseStatus(), 2, 16, QLatin1Char('0')).toUpper();
    }

    switch (m_state)
    {
    case WriteData:
    case ReadData:
        ui->status->setText(tr("failed %1").arg(status));
        break;

    case WriteConfig:
    case ReadConfig:
        ui->reportingStatus->setText(tr("failed %1").arg(status));
        break;

    default:
        break;
    }

    m_state = Idle;
    stateCheck();
}

void zmAttributeInfo::setAttribute(quint8 endpoint, quint16 clusterId, deCONZ::ZclClusterSide clusterSide, const deCONZ::ZclAttribute &attr)
{
    setWindowTitle(tr("Attribute Editor"));
    ui->attributeName->setText(attr.name());
    ui->attributeAccess->setText(attr.isReadonly() ? "read only" : "writeable");
    m_endpoint = endpoint;
    m_clusterId = clusterId;
    m_clusterSide = clusterSide;

    deCONZ::ZclDataType dataType = deCONZ::zclDataBase()->dataType(attr.dataType());

    QString text(tr("unknown"));
    if (dataType.isValid())
    {
        text = dataType.name();
    }

    text += QString(" (0x%2)").arg((uint)attr.dataType(), 0, 16);
    ui->attributeDataType->setText(text);

    if (attr.description().isEmpty())
    {
        ui->descriptionLabel->hide();
        ui->attributeDescription->hide();
    }
    else
    {
        ui->attributeDescription->setText(attr.description());
    }

    if (attr.minReportInterval() > 0 || attr.maxReportInterval() != 0xFFFF || attr.reportableChange().u64 != 0)
    {
        ui->minReportIntervalLineEdit->setText(QString::number(attr.minReportInterval()));
        ui->maxReportIntervalLineEdit->setText(QString::number(attr.maxReportInterval()));
        ui->reportableChangeLineEdit->setText(QString::number(attr.reportableChange().u64));
    }

    ui->reportableChangeLineEdit->setEnabled(dataType.isAnalog());

    m_attribute = attr;
    m_signed = false;

    switch (attr.dataType())
    {
    case deCONZ::ZclBoolean:
        buildBooleanInput();
        break;

    case deCONZ::Zcl8BitBitMap:
    case deCONZ::Zcl16BitBitMap:
    case deCONZ::Zcl24BitBitMap:
    case deCONZ::Zcl32BitBitMap:
    case deCONZ::Zcl40BitBitMap:
    case deCONZ::Zcl48BitBitMap:
    case deCONZ::Zcl56BitBitMap:
    case deCONZ::Zcl64BitBitMap:
        buildBitmapInput();
        break;

    case deCONZ::Zcl8BitEnum:
    case deCONZ::Zcl16BitEnum:
        buildEnumInput();
        break;

    case deCONZ::Zcl8BitInt:
    case deCONZ::Zcl16BitInt:
    case deCONZ::Zcl24BitInt:
    case deCONZ::Zcl32BitInt:
    case deCONZ::Zcl40BitInt:
    case deCONZ::Zcl48BitInt:
    case deCONZ::Zcl56BitInt:
    case deCONZ::Zcl64BitInt:
        m_signed = true;
        buildNumericInput();
        break;

    case deCONZ::Zcl8BitData:
    case deCONZ::Zcl16BitData:
    case deCONZ::Zcl24BitData:
    case deCONZ::Zcl32BitData:
    case deCONZ::Zcl40BitData:
    case deCONZ::Zcl48BitData:
    case deCONZ::Zcl56BitData:
    case deCONZ::Zcl64BitData:

    case deCONZ::Zcl8BitUint:
    case deCONZ::Zcl16BitUint:
    case deCONZ::Zcl24BitUint:
    case deCONZ::Zcl32BitUint:
    case deCONZ::Zcl40BitUint:
    case deCONZ::Zcl48BitUint:
    case deCONZ::Zcl56BitUint:
    case deCONZ::Zcl64BitUint:
    case deCONZ::ZclIeeeAddress:
    case deCONZ::Zcl128BitSecurityKey:
    case deCONZ::ZclSingleFloat:
    case deCONZ::ZclOctedString:
        buildNumericInput();
        break;

    default:
        break;
    }

    stateCheck();
}

void zmAttributeInfo::updateEdit()
{


    switch (m_attribute.dataType())
    {
    case deCONZ::ZclBoolean:
        setBooleanInput();
        break;

    case deCONZ::Zcl8BitBitMap:
    case deCONZ::Zcl16BitBitMap:
    case deCONZ::Zcl24BitBitMap:
    case deCONZ::Zcl32BitBitMap:
    case deCONZ::Zcl40BitBitMap:
    case deCONZ::Zcl48BitBitMap:
    case deCONZ::Zcl56BitBitMap:
    case deCONZ::Zcl64BitBitMap:
        setBitmapInput();
        break;

    case deCONZ::Zcl8BitEnum:
    case deCONZ::Zcl16BitEnum:
        setEnumInput();
        break;

    case deCONZ::Zcl8BitData:
    case deCONZ::Zcl16BitData:
    case deCONZ::Zcl24BitData:
    case deCONZ::Zcl32BitData:
    case deCONZ::Zcl40BitData:
    case deCONZ::Zcl48BitData:
    case deCONZ::Zcl56BitData:
    case deCONZ::Zcl64BitData:

    case deCONZ::Zcl8BitInt:
    case deCONZ::Zcl16BitInt:
    case deCONZ::Zcl24BitInt:
    case deCONZ::Zcl32BitInt:
    case deCONZ::Zcl40BitInt:
    case deCONZ::Zcl48BitInt:
    case deCONZ::Zcl56BitInt:
    case deCONZ::Zcl64BitInt:

    case deCONZ::Zcl8BitUint:
    case deCONZ::Zcl16BitUint:
    case deCONZ::Zcl24BitUint:
    case deCONZ::Zcl32BitUint:
    case deCONZ::Zcl40BitUint:
    case deCONZ::Zcl48BitUint:
    case deCONZ::Zcl56BitUint:
    case deCONZ::Zcl64BitUint:
    case deCONZ::ZclIeeeAddress:
    case deCONZ::Zcl128BitSecurityKey:
    case deCONZ::ZclSingleFloat:
    case deCONZ::ZclOctedString:
        setNumericInput();
        break;

    default:
        break;
    }
}

void zmAttributeInfo::zclWriteAttributeResponse(bool ok)
{
    m_timer->stop();
    m_state = Idle;

    if (ok)
    {
        ui->status->setText(tr("writing done"));
    }
    else
    {
        ui->status->setText(tr("writing failed"));
    }

    stateCheck();
}

void zmAttributeInfo::zclCommandResponse(const deCONZ::ZclFrame &zclFrame)
{
    switch (m_state)
    {
    case ReadData:
        readAttributeResponse(zclFrame);
        break;

    case WriteData:
        writeAttributeResponse(zclFrame);
        break;

    case ReadConfig:
        readReportConfigurationResponse(zclFrame);
        break;

    case WriteConfig:
        writeReportConfigurationResponse(zclFrame);
        break;

    default:
        return;
    }

    stateCheck();
}

void zmAttributeInfo::buildBooleanInput()
{
    QVBoxLayout *vbox = new QVBoxLayout(ui->valueWidget);
    QCheckBox *edit = new QCheckBox(m_attribute.name());
    edit->setChecked(m_attribute.numericValue().u8 == 1);
    vbox->addWidget(edit);
    m_edits.append(edit);
}

void zmAttributeInfo::buildNumericInput()
{
    QVBoxLayout *vbox = new QVBoxLayout(ui->valueWidget);
    QLineEdit *edit = new QLineEdit;
    vbox->addWidget(edit);

    deCONZ::ZclDataType dataType = deCONZ::zclDataBase()->dataType(m_attribute.dataType());
    QString str = m_attribute.toString(dataType, deCONZ::ZclAttribute::Prefix);

    edit->setText(str);
    m_edits.append(edit);
}

void zmAttributeInfo::buildBitmapInput()
{
    QVBoxLayout *vbox = new QVBoxLayout;
    ui->valueWidget->setLayout(vbox);
    QStringList names = m_attribute.valuesNames();
    std::vector<int> positions = m_attribute.valueNamePositions();

    if (names.isEmpty())
    {
        const deCONZ::ZclDataType &dataType = deCONZ::zclDataBase()->dataType(m_attribute.dataType());

        QString str = QString("%1").arg(m_attribute.bitmap(),
                                        (int)dataType.length() * 2,
                                        (int)16,
                                        QChar('0'));

        QLineEdit *edit = new QLineEdit(str);
        vbox->addWidget(edit);
        m_edits.append(edit);
    }
    else if (names.size() == (int)positions.size())
    {
        for (int i = 0; i < names.size(); i++)
        {
            QCheckBox *edit = new QCheckBox(names[i]);
            bool checked = m_attribute.bit(positions[i]);

            edit->setChecked(checked);
            vbox->addWidget(edit);
            m_edits.append(edit);
        }
    }
}

void zmAttributeInfo::buildEnumInput()
{
    QVBoxLayout *vbox = new QVBoxLayout(ui->valueWidget);
    QStringList names = m_attribute.valuesNames();
    std::vector<int> positions = m_attribute.valueNamePositions();
    QComboBox *edit = new QComboBox;

    if (names.isEmpty())
    {
        edit->setDisabled(true);
    }
    else if (names.size() == (int)positions.size())
    {
        edit->insertItems(0, names);
        for (uint i = 0; i < positions.size(); i++)
        {
            if (positions[i] == (int)m_attribute.enumerator())
            {
                edit->setCurrentIndex(i);
                break;
            }
        }
    }

    vbox->addWidget(edit);
    m_edits.append(edit);
}

bool zmAttributeInfo::getBooleanInput()
{
    QCheckBox *edit = qobject_cast<QCheckBox*>(m_edits.first());

    if (edit)
    {
        m_attribute.setValue(edit->isChecked());
        return true;
    }

    return false;
}

bool zmAttributeInfo::setBooleanInput()
{
    QCheckBox *edit = qobject_cast<QCheckBox*>(m_edits.first());

    if (edit)
    {
        edit->setChecked(m_attribute.numericValue().u8 == 0x01);
        return true;
    }

    return false;
}

bool zmAttributeInfo::getNumericInput()
{
    bool ok = false;
    QLineEdit *edit = qobject_cast<QLineEdit*>(m_edits.first());

    if (edit)
    {
        if (m_attribute.dataType() == deCONZ::Zcl128BitSecurityKey)
        {
            if (edit->text().size() != 32)
            {
                return false;
            }

            QByteArray key = QByteArray::fromHex(qPrintable(edit->text()));
            m_attribute.setValue(QVariant(key));
            return true;
        }
        else if (m_attribute.dataType() == deCONZ::ZclSingleFloat)
        {
            const float val = edit->text().toFloat(&ok);
            if (!ok)
            {
                return false;
            }
            m_attribute.setValue(QVariant(val));
            return true;
        }
        else if (m_attribute.dataType() == deCONZ::ZclOctedString)
        {
            if (!edit->text().startsWith("0x"))
                return false;

            const QByteArray hex = edit->text().mid(2).toLatin1();

            if (hex.isEmpty())
                return false;

            if (hex.size() & 1) // must be even number
                return false;

            ok = true;
            for (int i = 0; ok && i < hex.length(); i++)
            {
                const char ch = hex.at(i);
                if (!(
                      (ch >= '0' && ch <= '9') ||
                      (ch >= 'a' && ch <= 'f') ||
                      (ch >= 'A' && ch <= 'F')
                      ))
                    {
                        ok = false;
                    }
            }

            if (ok)
            {
                const QByteArray data = QByteArray::fromHex(hex);
                m_attribute.setValue(QVariant(data));
                return true;
            }
            return false;
        }

        if (m_signed)
        {
            qlonglong val = edit->text().toLongLong(&ok, m_attribute.numericBase());
            if (ok)
            {
                m_attribute.setValue(val);
                return true;
            }
        }
        else
        {
            qulonglong val = edit->text().toULongLong(&ok, m_attribute.numericBase());
            if (ok)
            {
                m_attribute.setValue(val);
                return true;
            }
        }
    }

    return false;
}

bool zmAttributeInfo::setNumericInput()
{
    QLineEdit *edit = qobject_cast<QLineEdit*>(m_edits.first());

    if (edit)
    {
        edit->setText(m_attribute.toString(deCONZ::ZclAttribute::Prefix));
        return true;
    }

    return false;
}

bool zmAttributeInfo::getBitmapInput()
{
    QStringList names = m_attribute.valuesNames();
    std::vector<int> positions = m_attribute.valueNamePositions();

    if (names.isEmpty())
    {
//        const deCONZ::ZclDataType &dataType = deCONZ::zclDataBase()->dataType(m_attribute.dataType());

//        QString str = QString("%1").arg(m_attribute.bitmap(),
//                                        (int)dataType.length() * 2,
//                                        (int)16,
//                                        QChar('0'));

//        QLineEdit *edit = new QLineEdit(str);
//        vbox->addWidget(edit);
//        m_edits.append(edit);
        return false;
    }
    else if (names.size() == (int)positions.size() && (names.size() == m_edits.size()))
    {
        for (int i = 0; i < names.size(); i++)
        {

            QCheckBox *edit = qobject_cast<QCheckBox*>(m_edits[i]);
            if (!edit)
            {
                return false;
            }

            m_attribute.setBit(positions[i], edit->isChecked());
        }

        return true;
    }

    return false;
}

bool zmAttributeInfo::setBitmapInput()
{
    QStringList names = m_attribute.valuesNames();
    std::vector<int> positions = m_attribute.valueNamePositions();

    if ((names.size() == (int)positions.size()) && (names.size() == m_edits.size()))
    {
        for (int i = 0; i < names.size(); i++)
        {
            QCheckBox *edit = qobject_cast<QCheckBox*>(m_edits[i]);
            if (edit)
            {
                edit->setChecked(m_attribute.bit(positions[i]));
            }
            else
            {
                return false;
            }
        }
        return true;
    }

    return false;
}

bool zmAttributeInfo::getEnumInput()
{
    QComboBox *edit = qobject_cast<QComboBox*>(m_edits.first());

    if (edit)
    {
        int idx = edit->currentIndex();

        if ((idx >= 0) && (idx < (int)m_attribute.valueNamePositions().size()))
        {
            idx  = m_attribute.valueNamePositions()[idx];
            m_attribute.setEnumerator(idx);
            return true;
        }
    }

    return false;
}

bool zmAttributeInfo::setEnumInput()
{
    QComboBox *edit = qobject_cast<QComboBox*>(m_edits.first());

    QStringList names = m_attribute.valuesNames();
    std::vector<int> positions = m_attribute.valueNamePositions();

    if (names.isEmpty())
    {
        edit->setDisabled(true);
    }
    else if (names.size() == (int)positions.size())
    {
        for (uint i = 0; i < positions.size(); i++)
        {
            if (positions[i] == (int)m_attribute.enumerator())
            {
                edit->setCurrentIndex(i);
                return true;
            }
        }
    }

    return false;
}

void zmAttributeInfo::readReportConfiguration()
{
    m_state = ReadConfig;
    ui->reportingStatus->setText(tr("reading ..."));
    emit zclReadReportConfiguration(m_attribute);
    stateCheck();
    m_timer->start();
}

/*!
  Attribute Reporting Configuration Response Record Field (section 2.4.10.1)

    1    |     1     |    2       |  0/1      | 0/2      | 0/2      | 0/variable |  0/2
  status | direction | attribute  | attribute | min      | max      | reportable | timeout
         |           | identifier | data type | interval | interval | change     |

  direction:
    0x00 vaules of the attribute are reported
         - data type
         - min interval
         - max interval
         - reportable change
    0x01 report of the attribute are received
         - timeout
 */
void zmAttributeInfo::readReportConfigurationResponse(const deCONZ::ZclFrame &zclFrame)
{
    if (zclFrame.commandId() == deCONZ::ZclDefaultResponseId)
    {
        if (zclFrame.defaultResponseCommandId() == deCONZ::ZclReadReportingConfigId)
        {
            if (zclFrame.defaultResponseStatus() != deCONZ::ZclSuccessStatus)
            {
                failedWithDefaultResponse(zclFrame);
                return;
            }
        }
    }

    QDataStream stream(zclFrame.payload());
    stream.setByteOrder(QDataStream::LittleEndian);

    quint8 status;
    quint8 direction;
    quint16 attrId;
    quint8 dataType;
    quint16 minInterval;
    quint16 maxInterval;

    stream >> status;
    stream >> direction;
    stream >> attrId;

    if (attrId == m_attribute.id())
    {
        m_state = Idle;

        if (status == deCONZ::ZclSuccessStatus)
        {
            if (direction == 0x00)
            {
                stream >> dataType;
                stream >> minInterval;
                stream >> maxInterval;

                ui->minReportIntervalLineEdit->setText(QString::number(minInterval));
                ui->maxReportIntervalLineEdit->setText(QString::number(maxInterval));

                deCONZ::ZclDataType type = deCONZ::zclDataBase()->dataType(dataType);

                if (type.isValid() && type.isAnalog())
                {
                    if (m_attribute.readReportableChangeFromStream(stream))
                    {
                        ui->reportableChangeLineEdit->setText(QString::number(m_attribute.reportableChange().u64));
                    }
                    else
                    {
                        ui->reportableChangeLineEdit->clear();
                    }
                    ui->reportableChangeLineEdit->setEnabled(true);
                }
                else
                {
                        ui->reportableChangeLineEdit->clear();
                        ui->reportableChangeLineEdit->setEnabled(false);
                }

                ui->reportingWidget->setEnabled(true);
                ui->reportingStatus->setText(tr("reading done"));
            }
        }
        else
        {
            ui->reportingWidget->setEnabled(false);
            deCONZ::Enumeration zclStatusEnum;

            if (deCONZ::zclDataBase()->getEnumeration(ZCL_ENUM, zclStatusEnum))
            {
                ui->reportingStatus->setText(zclStatusEnum.getValueName(status));
            }
        }
    }
}

void zmAttributeInfo::writeReportConfiguration()
{

    bool ok;
    quint16 min;
    quint16 max;

    min = ui->minReportIntervalLineEdit->text().toUShort(&ok);

    if (ok)
    {
        max = ui->maxReportIntervalLineEdit->text().toUShort(&ok);

        if (ok)
        {
            m_state = WriteConfig;
            ui->reportingStatus->setText(tr("writing ..."));

            m_attribute.setMinReportInterval(min);
            m_attribute.setMaxReportInterval(max);

            if (ui->reportableChangeLineEdit->isEnabled())
            {
                deCONZ::NumericUnion reportableChange;

                reportableChange.u64 = ui->reportableChangeLineEdit->text().toULongLong(&ok);

                if (!ok)
                {
                    reportableChange.u64 = 0;
                }
                m_attribute.setReportableChange(reportableChange);
            }

            emit zclWriteReportConfiguration(m_attribute, 0x00);
            stateCheck();
            m_timer->start();
        }
    }

}


void zmAttributeInfo::writeReportConfigurationResponse(const deCONZ::ZclFrame &zclFrame)
{
    if (zclFrame.commandId() == deCONZ::ZclDefaultResponseId)
    {
        if (zclFrame.defaultResponseCommandId() == deCONZ::ZclConfigureReportingId)
        {
            if (zclFrame.defaultResponseStatus() != deCONZ::ZclSuccessStatus)
            {
                failedWithDefaultResponse(zclFrame);
                return;
            }
        }
    }

    QDataStream stream(zclFrame.payload());
    stream.setByteOrder(QDataStream::LittleEndian);

    quint8 status;
    quint8 direction;
    quint16 attrId;

    stream >> status;
    stream >> direction;
    stream >> attrId;

    if (attrId == m_attribute.id() || stream.status() == QDataStream::ReadPastEnd)
    {
        m_state = Idle;

        if (status == deCONZ::ZclSuccessStatus)
        {
            if (direction == 0x00)
            {
                ui->reportingStatus->setText(tr("writing done"));
            }
        }
        else
        {
            deCONZ::Enumeration zclStatusEnum;

            if (deCONZ::zclDataBase()->getEnumeration(ZCL_ENUM, zclStatusEnum))
            {
                ui->reportingStatus->setText(zclStatusEnum.getValueName(status));
            }
        }
    }
}
