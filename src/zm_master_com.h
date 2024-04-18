/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_MASTER_COM_H_
#define ZM_MASTER_COM_H_

//#define USE_QSERIAL_PORT

#include <QObject>

#ifdef USE_QSERIAL_PORT
#include <QSerialPort>
#endif

struct zm_command;
class SerialComPrivate;

void COM_OnPacket(const zm_command *cmd); // defined in zm_master.cpp

class SerialCom : public QObject
{
     Q_OBJECT

public:
    SerialCom(QObject *parent = 0);
    ~SerialCom();
    /*!
       \brief Opens a serial device.

       The real open happens in another thread and the result will be
       reportet by signals connected() or on error disconnected().

       \param port - the full path to the serial device

       \return 0 if the device will be opened
              -1 some error occurred
     */
    int open(const QString &port, int baudrate);
    int close();
    int send(zm_command *cmd);
    bool isOpen();
    bool isApplicationConnected();

public Q_SLOTS:
    void readyRead();
    void bytesWritten(qint64 bytes);
    void timeout();
#ifdef USE_QSERIAL_PORT
    void handleError(QSerialPort::SerialPortError error);
#endif
    void processTh0Events();

Q_SIGNALS:
    void connected();
    void disconnected(int);
    void bootloaderStarted();
    void th0HasEvents();

protected:
    void timerEvent(QTimerEvent *event) override;

private:
    SerialComPrivate *d = nullptr;
};

#endif /* ZM_MASTER_COM_H_ */
