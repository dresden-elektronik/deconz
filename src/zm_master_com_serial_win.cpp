#include <QDebug>
#include <QTimerEvent>
#include <QMutexLocker>
#include <QRegExp>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
//#include <unistd.h>
#include <fcntl.h>
#include <windows.h>
#include "deconz/dbg_trace.h"
#include "zm_master_com.h"
#include "zm_master.h"
#include "zm_protocol.h"
#include "protocol.h"

#define MAX_SEND_LENGTH 196
#define MAX_SEND_QUEUE_SIZE 10
#define STATE_OFF 0
#define STATE_ON  1

enum ComState_t
{
    COM_OFF,
    COM_IDLE,
    COM_SEND
};

static SerialCom *Com = 0;
static uint8_t RxBuffer[1024];
static uint8_t TxBuffer[1024];
//static struct zm_command TxCmd;
static struct zm_command RxCmd;
static ComState_t ComState1 = COM_OFF;
static HANDLE hSerial = INVALID_HANDLE_VALUE;
static char rxChar;

static int errorHandler(const char *func, DWORD error);
static char SER_Getc(void);
static short SER_Putc(char c);
static char SER_Isc(void);
static void SER_Packet(uint8_t *data, uint16_t length);

enum ComState
{
    ComStateOff,
    ComStateOpen,
    ComStateOpenDone,
    ComStateRxTx,
    ComStateClose,
    ComStateCloseDone
};

struct SendBuffer
{
    SendBuffer() : length(0) { }

    uint16_t length;
    uint8_t data[MAX_SEND_LENGTH];
};

class SerialComPrivate
{
public:
    SerialComPrivate();
    void setState(ComState nextState);
    ComState state();

    ComState comState;
    SerialCom *p;
    QString port;
    uint8_t m_protId;
    int closeReason;
    QMutex sendMutex;
    std::queue<SendBuffer> sendQueue;
};


SerialCom::SerialCom(QObject *parent) :
    QObject(parent)
{
    Q_ASSERT(Com == 0 || "only one SerialCom instance allowed");
    Com = this;
    m_protId = PROTO_NO_PROTOCOL;
    d = new SerialComPrivate;
    d->p = this;
}

SerialCom::~SerialCom()
{
    close();
    Com = 0;
    ComState1 = COM_OFF;
    delete d;
    d = 0;
}

int SerialCom::open(const QString &port)
{
    QMutexLocker lock(&m_mutex);

    if (ComState1 != COM_OFF)
    {
        DBG_Printf(DBG_ERROR, "COM already connected\n");
        return -1;
    }

    d->port = port;

    DCB dcbSerial = {0};

    QRegExp rx("^COM(\\d+)");
    QString fullName(port);
    if(fullName.contains(rx)) {
        int portnum = rx.cap(1).toInt();
        if(portnum > 9) // COM ports greater than 9 need \\.\ prepended
            fullName.prepend("\\\\.\\");
    }

    hSerial = CreateFileA(qPrintable(fullName),
                         GENERIC_READ | GENERIC_WRITE,
                         0, // shae 0, cannot share COM port
                         0, // security none
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         0 // no templates file for com port
                         );

    if (hSerial != INVALID_HANDLE_VALUE)
    {
        dcbSerial.DCBlength = sizeof(dcbSerial);
        if (!GetCommState(hSerial, &dcbSerial))
        {
            DBG_Printf(DBG_ERROR, "COM can't get com parameters\n");
            return -1;
        }

        // settings for target platform
        dcbSerial.BaudRate = CBR_38400;
        dcbSerial.ByteSize = 8;
        dcbSerial.StopBits = ONESTOPBIT;
        dcbSerial.Parity = NOPARITY;
        //dcbSerial.fRtsControl = RTS_CONTROL_TOGGLE;
        dcbSerial.fRtsControl = RTS_CONTROL_DISABLE;

        // settings for FTDI
//        dcbSerial.BaudRate = CBR_115200;
//        dcbSerial.ByteSize = 8;
//        dcbSerial.StopBits = ONESTOPBIT;
//        dcbSerial.Parity = NOPARITY;
//        dcbSerial.fRtsControl = RTS_CONTROL_DISABLE;

        if (!SetCommState(hSerial, &dcbSerial))
        {
            DBG_Printf(DBG_ERROR, "COM can't set com parameters\n");
            return -1;
        }

        // setting timeouts
        COMMTIMEOUTS timeouts = {0};

        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutConstant = 0;
        timeouts.ReadTotalTimeoutMultiplier = 0;

        timeouts.WriteTotalTimeoutConstant = 0;
        timeouts.WriteTotalTimeoutMultiplier = 0;

        if (!SetCommTimeouts(hSerial, &timeouts))
        {
            DBG_Printf(DBG_ERROR, "COM can't set com timouts\n");
            return -1;
        }

        ComState1 = COM_IDLE;
        DBG_Printf(DBG_INFO, "COM Connected\n");

        protocol_init();

        m_protId = protocol_add(PROTO_RX | PROTO_TX | PROTO_FLAGGED | PROTO_TRACE,
                                      SER_Getc, SER_Isc, SER_Putc, 0, SER_Packet);
        protocol_set_buffer(m_protId, RxBuffer, sizeof(RxBuffer));
        d->setState(ComStateOpenDone); // TODO put all in d->open()
        return 0;
    }
    else
    {
        if (GetLastError() == ERROR_NOT_FOUND)
        {
            DBG_Printf(DBG_ERROR, "COM port %s not found\n", qPrintable(port));
        }
        else
        {
            DBG_Printf(DBG_ERROR, "COM open com port failed error: %d\n", GetLastError());
        }
    }

    return -1;
}

int SerialCom::close()
{
    if (m_protId != PROTO_NO_PROTOCOL)
    {
        protocol_remove(m_protId);
        m_protId = PROTO_NO_PROTOCOL;
    }

    if (hSerial != INVALID_HANDLE_VALUE)
    {
        protocol_exit();
        d->setState(ComStateClose);
    }
    return 0;
}

void SerialCom::work()
{
    m_work = true;

    while (m_work)
    {
        switch (d->state())
        {
//        case ComStateOpen:
//            break;

        case ComStateOpenDone:
            d->setState(ComStateRxTx);
            emit connected();
            break;

        case ComStateCloseDone:
            d->setState(ComStateOff);
            emit disconnected(d->closeReason);
            break;

        case ComStateClose:
        {
            if (hSerial != INVALID_HANDLE_VALUE)
            {
                CloseHandle(hSerial);
                hSerial = INVALID_HANDLE_VALUE;
            }

            ComState1 = COM_OFF;
            d->setState(ComStateCloseDone);
        }
            break;

        default:
            break;
        }

        if (isConnected())
        {
            DWORD ret = WaitForSingleObject(hSerial, 10);
            if (ret == WAIT_OBJECT_0) // ready
            {
                receive();
            }
            else if (ret == WAIT_FAILED)
            {
                DBG_Printf(DBG_ERROR, "%s WAIT_FAILED\n", Q_FUNC_INFO);
                this->close();
            }
            Sleep(5);
        }
        else
        {
            Sleep(10);
        }
    }

    DBG_Printf(DBG_INFO, "Serial com stopped\n");
}

/*!
    Quits the work loop in the next iteration.
 */
void SerialCom::stopWork()
{
    m_work = false;
}

void SerialCom::onPacket(zm_command *cmd)
{
    m_mutex.lock();
    m_inQueue.push(*cmd);
    m_mutex.unlock();
    emit gotPacket();
}

bool SerialCom::getNextPacket(zm_command *cmd)
{
    QMutexLocker lock(&m_mutex);

    if (!m_inQueue.empty())
    {
        const zm_command &pck = m_inQueue.front();
        memcpy(cmd, &pck, sizeof(*cmd));
        m_inQueue.pop();
        return true;
    }

    return false;
}

int SerialCom::send(zm_command *cmd)
{
    if (ComState1 != COM_OFF)
    {
        quint16 len = zm_protocol_command2buffer(cmd, 0x1000, TxBuffer, sizeof(TxBuffer));
        if (len > 0)
        {
            ComState1 = COM_SEND;
            protocol_send(m_protId, TxBuffer, len);

            // check if error occurred during sending
            if (ComState1 == COM_OFF)
            {
                return -1;
            }

            ComState1 = COM_IDLE;
            return 0;
        }
    }

    return -1;
}

void SerialCom::receive()
{
    if (ComState1 == COM_IDLE)
    {
        protocol_receive(m_protId);
    }
}

bool SerialCom::isConnected()
{
    if (m_work)
    {
        if (hSerial != INVALID_HANDLE_VALUE)
        {
            return true;
        }
    }

    return false;
}

SerialComPrivate::SerialComPrivate()
{
    comState = ComStateOff;
    closeReason = deCONZ::DeviceDisconnectNormal;
}

void SerialComPrivate::setState(ComState nextState)
{
    comState = nextState;
}

ComState SerialComPrivate::state()
{
    return comState;
}


static int errorHandler(const char *func, DWORD error)
{
    switch (error)
    {
    case ERROR_IO_PENDING:
        return 0;

    default:
    {
        DBG_Printf(DBG_ERROR, "COM %s error: 0x%X\n", func, error);
        if (Com)
        {
            Com->close();
        }
    }
        break;
    }

    return -1;
}

static char SER_Getc(void)
{
    if (hSerial != INVALID_HANDLE_VALUE)
    {
        char c = rxChar;
        DBG_Printf(DBG_WIRE, "%02X\n", c & 0xFF);
        return c;
    }

    return 0;
}

static short SER_Putc(char c)
{
    if (hSerial != INVALID_HANDLE_VALUE)
    {
        DWORD nwritten;
        if (!WriteFile(hSerial, &c, 1, &nwritten, NULL))
        {
            errorHandler("WriteFile", GetLastError());
        }
        else
        {
            if (nwritten != 1)
            {
                DBG_Printf(DBG_ERROR, "COM error on write 1 byte\n");
            }
            else
            {
                return 1;
            }
        }
    }

    return 0;
}

static char SER_Isc(void)
{
    if (hSerial != INVALID_HANDLE_VALUE)
    {
        DWORD nread;
        if (ReadFile(hSerial, &rxChar, sizeof(rxChar), &nread, NULL))
        {
            return nread > 0 ? 1 : 0;
        }
        else
        {
            errorHandler("ReadFile", GetLastError());
        }
    }

    return 0;
}

static void SER_Packet(uint8_t *data, uint16_t length)
{
    if (zm_protocol_buffer2command(data, length, 0x1000, &RxCmd) != 0)
    {
        Com->onPacket(&RxCmd);
    }
}
