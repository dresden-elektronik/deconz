#include <queue>
#include <QDebug>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <linux/serial.h>
#include <asm/ioctls.h>
#include <asm/termbits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "deconz/dbg_trace.h"
#include "deconz/util.h"
#include "zm_master_com.h"
#include "zm_master.h"
#include "zm_protocol.h"
#include "protocol.h"

#define RXTX_SLEEP_US (1000 * 3) // 3 ms
#define RXTX_SELECT_SLEEP_US (1000 * 2) // 2 ms
#define MAX_SEND_LENGTH 196
#define MAX_SEND_QUEUE_SIZE 10

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
    int open();
    int close();
    int rxtx();
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

static SerialCom *Com = 0;
static int comFd = -1;
static uint8_t RxBuffer[1024];
static struct zm_command RxCmd;

static char SER_Getc(void);
static short SER_Putc(char c);
static char SER_Isc(void);
static void SER_Packet(uint8_t *data, uint16_t length);

SerialCom::SerialCom(QObject *parent) :
    QObject(parent)
{
    d = new SerialComPrivate;
    d->p = this;

    Q_ASSERT(Com == 0 || "only one SerialCom instance allowed");
    comFd = -1;
    m_protId = PROTO_NO_PROTOCOL;
    Com = this;
}

SerialCom::~SerialCom()
{
    this->close();
    Com = 0;
    delete d;
    d = 0;
}

int SerialCom::open(const QString &port)
{
    if (DBG_Assert(!port.isEmpty()) == false)
    {
        return -1;
    }

    if (!isConnected())
    {
        d->port = port;
        d->setState(ComStateOpen);
        return 0;
    }

    return -1;
}

int SerialCom::close(void)
{
    if (DBG_Assert(isConnected()) == false)
    {
        return -1;
    }

    if (m_work)
    {
        d->setState(ComStateClose);
    }

    return 0;
}

bool SerialCom::isConnected()
{
    if (m_work)
    {
        if (d->state() == ComStateRxTx)
        {
            if (comFd != -1)
            {
                return true;
            }
        }
    }

    return false;
}

void SerialCom::work()
{
    m_work = true;

    while (m_work)
    {
        switch (d->state())
        {
        case ComStateOff:
        {
            if (usleep(RXTX_SLEEP_US) == -1)
            {
                DBG_Printf(DBG_ERROR, "%s usleep() : %s\n", Q_FUNC_INFO, strerror(errno));
            }
        }
            break;

        case ComStateRxTx:
        {
            d->rxtx();
        }
            break;

        case ComStateOpen:
        {
            if (d->open() == 0)
            {
                d->setState(ComStateOpenDone);
            }
            else
            {
                d->setState(ComStateCloseDone);
            }
        }
            break;

        case ComStateOpenDone:
        {
            d->setState(ComStateRxTx);
            emit connected();
        }
            break;

        case ComStateClose:
        {
            d->close();
            d->setState(ComStateCloseDone);
        }
            break;

        case ComStateCloseDone:
        {
            d->setState(ComStateOff);
            emit disconnected(d->closeReason);
        }
            break;

        default:
        {
            d->setState(ComStateOff);
        }
            break;
        }
    }
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
    DBG_Printf(DBG_PROT_L2, "SerialCom::onPacket cmd: 0x%02X, seq: 0x%u\n", cmd->cmd, cmd->seq);
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
    QMutexLocker lock(&d->sendMutex);

    if (d->sendQueue.size() > MAX_SEND_QUEUE_SIZE)
    {
        return -1;
    }

    if (isConnected())
    {
        SendBuffer buf;

        buf.length = zm_protocol_command2buffer(cmd, 0x1000, buf.data, sizeof(buf.data));
        if (buf.length > 0)
        {
            d->sendQueue.push(buf);
            return 0;
        }

    }

    return -1;
}

void SerialCom::receive()
{
    // TODO remove receive()
}

SerialComPrivate::SerialComPrivate()
{
    comState = ComStateOff;
    closeReason = deCONZ::DeviceDisconnectNormal;
}

int SerialComPrivate::open()
{
    // check if we have permissions to open the serial device
    if (access(qPrintable(port), R_OK | W_OK) == -1)
    {
        DBG_Printf(DBG_ERROR, "%s error access(): %s\n", Q_FUNC_INFO, strerror(errno));
        closeReason = deCONZ::DeviceDisconnectNoPermisson;
        return -1;
    }

    const char *baudcmd = "--baudrate"; // commandline parameter

    int oflags = 0;
    oflags |= O_RDWR;
    oflags |= O_NOCTTY; // no console
//    oflags |= O_NONBLOCK;
    oflags |= O_NDELAY; // no blocking
    //oflags |= O_SYNC; // always wait until write finishes

    comFd = ::open(qPrintable(port), oflags);

    if (comFd == -1)
    {
        DBG_Printf(DBG_ERROR, "%s error open(): %s\n", Q_FUNC_INFO, strerror(errno));
        closeReason = deCONZ::DeviceDisconnectIoError;
        return -1;
    }
    else
    {
        DBG_Printf(DBG_INFO, "%s com opened %s, fd: %d\n", Q_FUNC_INFO, qPrintable(port), comFd);
    }

    struct termios2 attr;
    speed_t speed = 38400;

    bzero(&attr, sizeof(attr));

    if (ioctl(comFd, TCGETS2, &attr) == -1)
    {
        DBG_Printf(DBG_ERROR, "%s error ioctl(TCSETS2): %s\n", Q_FUNC_INFO, strerror(errno));
        goto error0;
    }

    switch (deCONZ::appArgumentNumeric(baudcmd, 0))
    {
    case 0: // default, fall through
    case 38400:  speed = 38400;  break;
    case 76800:  speed = 76800;  break;
    case 115200: speed = 115200; break;
    default: // unsupported
    {
        DBG_Printf(DBG_ERROR, "unsupportet value: %s\n", baudcmd);
        speed = 38400;
    }
        break;
    }

    attr.c_ispeed = speed;
    attr.c_ospeed = speed;
    attr.c_cflag &= ~CBAUD;
    attr.c_cflag |= BOTHER;

    //cfmakeraw(&attr);

    // manual make raw (parameters from man page)
    attr.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    attr.c_oflag &= OPOST;
    attr.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    attr.c_cflag &= ~(CSIZE | PARENB);
    attr.c_cflag |= CS8;

    if (ioctl(comFd, TCSETS2, &attr) == -1)
    {
        DBG_Printf(DBG_ERROR, "%s error ioctl(TCSETS2): %s\n", Q_FUNC_INFO, strerror(errno));
        goto error0;
    }

    // init protocol
    protocol_init();
    m_protId = protocol_add(PROTO_RX | PROTO_TX | PROTO_FLAGGED | PROTO_TRACE,
                                  SER_Getc, SER_Isc, SER_Putc, 0, SER_Packet);
    protocol_set_buffer(m_protId, RxBuffer, sizeof(RxBuffer));

    return 0;

error0:
    // fd is opened so close it
    ::close(comFd);
    comFd = -1;
    closeReason = deCONZ::DeviceDisconnectIoError;
    return -1;
}

int SerialComPrivate::close()
{
    if (m_protId != PROTO_NO_PROTOCOL)
    {
        protocol_remove(m_protId);
        m_protId = PROTO_NO_PROTOCOL;
        protocol_exit();
    }

    if (comFd != -1)
    {
        if (::close(comFd) == -1)
        {
            DBG_Printf(DBG_ERROR, "%s error close(): %s\n", Q_FUNC_INFO, strerror(errno));
        }
        comFd = -1;
        return 0;
    }

    return -1;
}

int SerialComPrivate::rxtx()
{
    int r;
    fd_set rdfds;
    fd_set wrfds;
    struct timeval tv;
    bool worked = false;

    if (p->isConnected())
    {
        FD_ZERO(&rdfds);
        FD_ZERO(&wrfds);
        FD_SET(comFd, &rdfds);
        FD_SET(comFd, &wrfds);

        // tv must be set each time because select() may modify it
        tv.tv_sec = 0;
        tv.tv_usec = RXTX_SELECT_SLEEP_US;

        r = select(comFd + 1, &rdfds, &wrfds, NULL, &tv);

        if ((r == -1) && (errno == EINTR))
        {
            return 0; // signal occured, retry later
        }

        if (r == -1) {
            DBG_Printf(DBG_ERROR, "%s select() : %s\n", Q_FUNC_INFO, strerror(errno));
            closeReason = deCONZ::DeviceDisconnectIoError;
            setState(ComStateClose);
            return -1;
        }
        else
        {
            if (p->isConnected() && FD_ISSET(comFd, &rdfds))
            {
                protocol_receive(m_protId);
                worked = true;
            }

            if (p->isConnected() && FD_ISSET(comFd, &wrfds))
            {
                // copy to new buffer to prevent GUI waiting for unlock while sending
                SendBuffer buf;
                sendMutex.lock();
                if (!sendQueue.empty())
                {
                    buf = sendQueue.front();
                    sendQueue.pop();
                }
                sendMutex.unlock();

                if (buf.length > 0)
                {
                    protocol_send(m_protId, buf.data, buf.length);
                    worked = true;
                }
            }

            if (!worked)
            {
                if (usleep(RXTX_SLEEP_US) == -1)
                {
                    DBG_Printf(DBG_ERROR, "%s usleep() : %s\n", Q_FUNC_INFO, strerror(errno));
                }
            }
        }

        return 0;
    }
    else
    {
        DBG_Printf(DBG_ERROR, "%s error disconnected while rxtx()\n", Q_FUNC_INFO);
        closeReason = deCONZ::DeviceDisconnectNormal;
        setState(ComStateClose);
    }

    return -1;
}

void SerialComPrivate::setState(ComState nextState)
{
    comState = nextState;
}

ComState SerialComPrivate::state()
{
    return comState;
}

static char SER_Getc(void)
{
    if (Com->isConnected())
    {
        char c;

        int ret = ::read(comFd, &c, 1);
        if (ret == 1)
        {
            DBG_Printf(DBG_WIRE, "%02X\n", c & 0xFF);
            return c;
        }

        if (ret == -1)
        {
            DBG_Printf(DBG_ERROR, "%s error read(): %s\n", Q_FUNC_INFO, strerror(errno));

            switch (errno)
            {
            case EBADFD:
                comFd = -1;
                break;

            default:
                break;
            }

//            Com->close();
        }
    }

    return 0x00;
}

static char SER_Isc(void)
{
    if (Com->isConnected())
    {
        int n;
        if (::ioctl(comFd, FIONREAD, &n) == -1)
        {
            DBG_Printf(DBG_ERROR, "%s error ioctl(): %s\n", Q_FUNC_INFO, strerror(errno));
            n = 0;

            switch(errno)
            {
            case EBADFD:
                comFd = -1;
                break;

            default:
                break;
            }
        }

        if (n > 0)
        {
            return 1;
        }
    }

    return 0;
}

short SER_Putc(char c)
{
    if (Com->isConnected())
    {
        int r;
        fd_set wrfds;
        struct timeval tv;
        int retry = 0;

        while (retry < 10)
        {
            FD_ZERO(&wrfds);
            FD_SET(comFd, &wrfds);

            // tv must be set each time because select() may modify it
            tv.tv_sec = 0;
            tv.tv_usec = 1000 * 10;

            r = select(comFd + 1, NULL, &wrfds, NULL, &tv);

            if ((r == -1) && (errno == EINTR))
            {
                continue;
            }

            if (r == -1)
            {
                DBG_Printf(DBG_ERROR, "COM error select(): %s\n", strerror(errno));
                comFd = -1; // note: no close since it will block?!
                return 0;
            }

            if (FD_ISSET(comFd, &wrfds))
            {
                ssize_t ret = ::write(comFd, &c, 1);

                if (ret == 1)
                {
                    DBG_Printf(DBG_WIRE, "%02X\n", c & 0xFF);
                    return 1;
                }
                else if (ret == -1)
                {
                    int err = errno;

                    switch (errno)
                    {
                    case EAGAIN:
                        // wait 5 ms
                        if (usleep(1000 * 5) == -1)
                        {
                            DBG_Printf(DBG_ERROR, "COM usleep() : %s\n", strerror(err));
                        }
                        retry++;
                        break;

                    case EBADFD:
                        comFd = -1;
                        // fall through

                    default:
                        DBG_Printf(DBG_ERROR, "COM error(%d) write() retry: %d, %s\n", err, retry, strerror(err));
                        return 0;
                    }
                }
                else
                {
                    DBG_Printf(DBG_ERROR, "COM failed write(): ret = %d\n", ret);
                    retry++;
                }
            }
            else
            {
                DBG_Printf(DBG_ERROR, "COM fd not ready to write(): retry = %d\n", retry);
                retry++;
            }
        }

        DBG_Printf(DBG_ERROR, "COM SER_Putc() giveup\n");
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
