/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <array>
#include <cassert>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <QCoreApplication>
#include <QTimer>
#include <QEvent>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#ifdef Q_OS_UNIX
// native calls
#include <poll.h>
#include <fcntl.h> /* open() */
#include <unistd.h> /* close() */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h> /* POSIX terminal control definitions */
#endif

#include "deconz/dbg_trace.h"
#include "deconz/util.h"
#include "zm_master_com.h"
#include "zm_master.h"
#include "common/protocol.h"

#define RX_EVENT_ID  0x01
#define TX_EVENT_ID  0x02
#define ERR_EVENT_ID 0x04
#define TH0_EVENT_ID 0x08

//#define DBG_SERIAL

#ifdef DECONZ_DEBUG_BUILD

#if _MSC_VER
  #define M_ASSERT(c) if (!(c)) __debugbreak()
#elif __GNUC__
  #define M_ASSERT(c) if (!(c)) __builtin_trap()
#else
  #define M_ASSERT assert
#endif

#endif

#ifndef DECONZ_DEBUG_BUILD

#undef DBG_Printf
#define DBG_Printf(...) do {} while(0)

#define M_ASSERT(c) DBG_Assert(c)


#endif

#define TH_RX_BUFFER_SIZE 2048
#define RX_BUFFER_SIZE 256
#define TX_BUFFER_SIZE 1024
#define MAX_SEND_LENGTH 196
#define MAX_SEND_QUEUE_SIZE 4

enum ComState
{
    ComStateOff,
    ComStateQueryBootloader,
    ComStateWaitBootloader,
    ComStateRxTx
};

struct TrxBuffer
{
    uint16_t length;
    uint8_t data[MAX_SEND_LENGTH];
};

class SerialComPrivate
{
public:
    int open(int baudrate);
    int close();
    int rx();
    int tx();
    void flush();
    void queryBootloader();
    void setState(ComState nextState);
    ComState state();
    void checkBootloader();
    ComState comState = ComStateOff;
    SerialCom *q = nullptr;
#ifdef USE_QSERIAL_PORT
    QSerialPort *serialPort = nullptr;
#endif
    uint8_t protId;
    size_t rxBytes = 0;
    int closeReason = deCONZ::DeviceDisconnectNormal;
    bool btlResponse = false;
    QString port;
    int pollTimerId;
    QTimer timer;
    size_t rxReadPos = 0;
    size_t rxWritePos = 0;
    size_t txReadPos = 0;
    size_t txWritePos = 0;
    std::array<uint8_t, RX_BUFFER_SIZE> rxBuffer;
    std::array<char, TX_BUFFER_SIZE> txBuffer;
};

#ifndef USE_QSERIAL_PORT
struct Platform
{
    int fd;
    int baudrate;
};

static struct Platform platform;
#endif

static SerialCom *Com = 0;
static SerialComPrivate *ComPriv = 0;

static uint8_t PROT_RxBuffer[256];

static uint8_t sendPos = 0;
static uint8_t sendEnd = 0;
static TrxBuffer sendQueue[MAX_SEND_QUEUE_SIZE];

static char SER_Getc(void);
static short SER_Putc(char c);
static char SER_Isc(void);
static void SER_Flush();
static void SER_Packet(uint8_t *data, uint16_t length);

static int PL_IsConnected();
static int PL_Connect(const char *path, int baudrate);
static void PL_Disconnect();
static int PL_BytesToWrite();
static int PL_Read(void *buf, int maxsize);
static int PL_Write(const void *buf, int size);
static void PL_Poll();

static void TXQ_Init(void);
static int TXQ_IsEmpty(void);
static int TXQ_IsFull(void);
static unsigned TXQ_Push(void);
static unsigned TXQ_Pop(void);
static void TXQ_Test(void);

SerialCom::SerialCom(QObject *parent) :
    QObject(parent)
{
    d = new SerialComPrivate;
    d->q = this;
    d->pollTimerId = -1;

    M_ASSERT(Com == nullptr || "only one SerialCom instance allowed");
    d->protId = PROTO_NO_PROTOCOL;
    Com = this;
    ComPriv = d;

    d->timer.setSingleShot(true);
    connect(&d->timer, &QTimer::timeout, this, &SerialCom::timeout);
    connect(this, &SerialCom::th0HasEvents, this, &SerialCom::processTh0Events, Qt::QueuedConnection);
#ifdef USE_QSERIAL_PORT
    d->serialPort = new QSerialPort(this);
    connect(d->serialPort, &QSerialPort::readyRead, this, &SerialCom::readyRead);
    connect(d->serialPort, &QSerialPort::bytesWritten, this, &SerialCom::bytesWritten);
#endif

    TXQ_Test();
}

#ifdef USE_QSERIAL_PORT

static int PL_IsConnected()
{
    return ComPriv->serialPort->isOpen() ? 1 : 0;
}

static int PL_Connect(const char *path, int baudrate)
{
    ComPriv->serialPort->setPortName(QLatin1String(path));

    auto bd = QSerialPort::UnknownBaud;

    switch (baudrate)
    {
    case 0: // default, fall through
    [[clang::fallthrough]];
    case 38400:  bd = QSerialPort::Baud38400;  break;
    case 115200:  bd = QSerialPort::Baud115200;  break;
    default: // unsupported
    {
        DBG_Printf(DBG_ERROR, "[COM] unsupported --baudrate value\n");
    }
        return 0;
    }

    ComPriv->serialPort->setBaudRate(bd);

    if (!ComPriv->serialPort->open(QSerialPort::ReadWrite))
    {
        ComPriv->closeReason = deCONZ::DeviceDisconnectIoError;
        DBG_Printf(DBG_ERROR_L2, "[COM] failed to open %s: %s\n", path, qPrintable(ComPriv->serialPort->errorString()));
        return 0;
    }

    return 1;
}

static void PL_Disconnect()
{
    ComPriv->serialPort->close();
}

static int PL_BytesToWrite()
{
    return ComPriv->serialPort->bytesToWrite();
}

static int PL_Read(void *buf, int maxsize)
{
    return ComPriv->serialPort->read((char*)buf, maxsize);
}

static int PL_Write(const void *buf, int size)
{
    return ComPriv->serialPort->write((char*)buf, size);
}

static void PL_Poll()
{
}

#endif

#if defined(Q_OS_UNIX) && !defined(USE_QSERIAL_PORT)

struct PL_Thread
{
    std::thread th;

    std::atomic_uint events;
    std::atomic_bool running;
    std::mutex mtx_fd;
    std::mutex mtx_rx;
    std::mutex mtx_tx;

    std::condition_variable cv;

    // circular buffer pointers
    unsigned rx_a; // read pointer
    unsigned rx_b; // write pointer

    uint8_t rxbuf[TH_RX_BUFFER_SIZE];
};

static PL_Thread *plThread;

static void PL_Thread0()
{
    {
        std::lock_guard<std::mutex> lock(plThread->mtx_fd);
        plThread->running = true;
        plThread->rx_a = 0;
        plThread->rx_b = 0;
        plThread->events = 0;
    }

    using namespace std::chrono_literals;

    M_ASSERT(platform.fd != 0);

    struct pollfd fds;

    int nread = 0;
    int timeout = 2;

    for (;;)
    {
        int ret = 0;

        {
            if (!plThread->running)
                break;

            if (plThread->events & TX_EVENT_ID)
            {
                std::unique_lock<std::mutex> tx_lock(plThread->mtx_tx);

                if (TXQ_IsEmpty())
                {
                    plThread->events &= ~TX_EVENT_ID;
                }
                else
                {
                    ComPriv->tx();
                }
            }

            fds.events = POLLIN;
            fds.fd = platform.fd;
            ret = poll(&fds, 1 /* nfds*/ , timeout); // ret == 0: timeout | < 0 error

            std::lock_guard<std::mutex> rx_lock(plThread->mtx_rx);

            if (ret > 0)
            {
                if (fds.revents & (POLLHUP | POLLERR | POLLNVAL))
                {
                    plThread->events |= ERR_EVENT_ID;
                }
                else if (fds.revents & POLLIN)
                {
                    unsigned maxsize = 0;

                    {
                        unsigned rxa = plThread->rx_a;
                        unsigned rxb = plThread->rx_b;

                        if (rxa == rxb)
                        {
                            maxsize = sizeof(plThread->rxbuf);
                        }
                        else
                        {
                            for (;(rxb + 1) != rxa; rxb++)
                            {
                                maxsize++;
                                if (maxsize == sizeof(plThread->rxbuf))
                                        break;

                                if (maxsize == 128)
                                    break;
                            }

                            M_ASSERT(maxsize <= sizeof(plThread->rxbuf));
                        }
                    }

                    if (maxsize > 0)
                    {
                        unsigned char buf[128];

                        if (sizeof(buf) < maxsize)
                            maxsize = sizeof(buf);

                        nread = read(platform.fd, &buf[0], maxsize);
                        M_ASSERT(nread <= (int)maxsize);

                        if (nread > 0 && nread <= (int)maxsize)
                        {
                            int mkEvent = 0;

                            M_ASSERT(nread <= (int)sizeof(buf));
                            for (int i = 0; i < nread; i++)
                            {
                                M_ASSERT(plThread->rx_b % TH_RX_BUFFER_SIZE < sizeof(plThread->rxbuf));
                                M_ASSERT((plThread->rx_b + 1) != plThread->rx_a);
                                plThread->rxbuf[plThread->rx_b % TH_RX_BUFFER_SIZE] = buf[i];
                                plThread->rx_b++;

                                // #1 if we detect end marker emit rx
                                if (buf[i] == 0xC0)
                                    mkEvent++;
                            }

                            DBG_Printf(DBG_PROT, "[TH0] rx %d bytes, make event: %d\n", nread, mkEvent);

                            // #2 rx buffer full
                            if (((plThread->rx_b + 1) % TH_RX_BUFFER_SIZE) == (plThread->rx_a % TH_RX_BUFFER_SIZE))
                            {
                                mkEvent = 1;
                                // DBG_Printf(DBG_ERROR, "[TH0] rx buffer full ...\n");
                            }

                            if (mkEvent)
                            {
                                plThread->events |= RX_EVENT_ID;
                            }
                        }
                        else if (nread == -1)
                        {
                            if ((errno == EINTR || errno == EWOULDBLOCK) == 0)
                            {
                                DBG_Printf(DBG_ERROR, "[TH0] error read(): %s\n", strerror(errno));
                                plThread->events |= ERR_EVENT_ID;
                            }
                        }
                    }
                }
            }
            else if (ret < 0)
            {
                if (errno == EINTR)
                {
                }
                else if ((plThread->events & ERR_EVENT_ID) == 0)
                {
                    DBG_Printf(DBG_ERROR, "[TH0] error poll(): %s\n", strerror(errno));
                    plThread->events |= ERR_EVENT_ID;
                    // now wait until .running is set to false
                }
            }

            if ((plThread->events & TH0_EVENT_ID) == 0) // not signalled to main thread yet
            {
                if ((plThread->events & (RX_EVENT_ID | ERR_EVENT_ID)) != 0)
                {
                    plThread->events |= TH0_EVENT_ID;
                    emit Com->th0HasEvents();
                }
            }
        }
    }
}

static int PL_IsConnected()
{
    return plThread == nullptr ? 0 : 1;
}

// https://tldp.org/HOWTO/Serial-Programming-HOWTO/x115.html
static int plSetupPort(int fd, int baudrate)
{
    struct termios options1;

    memset(&options1, 0, sizeof(options1));
    //fcntl(fd, F_SETFL, O_RDWR | O_NONBLOCK | O_NOCTTY);

    if (ioctl(fd, TIOCEXCL) == -1)
    {
        DBG_Printf(DBG_ERROR, "[COM] error setting TIOCEXCL: %s (%d)\n", strerror(errno), errno);
    }

#if 0
    struct termios tty;

    // Read in existing settings, and handle any error
    if(tcgetattr(fd, &tty) != 0)
    {
        printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
        return 1;
    }

    tty.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
    tty.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
    tty.c_cflag &= ~CSIZE; // Clear all bits that set the data size
    tty.c_cflag |= CS8; // 8 bits per byte (most common)
    tty.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control (most common)
    tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)

    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO; // Disable echo
    tty.c_lflag &= ~ECHOE; // Disable erasure
    tty.c_lflag &= ~ECHONL; // Disable new-line echo
    tty.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes

    tty.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
    tty.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed
    // tty.c_oflag &= ~OXTABS; // Prevent conversion of tabs to spaces (NOT PRESENT ON LINUX)
    // tty.c_oflag &= ~ONOEOT; // Prevent removal of C-d chars (0x004) in output (NOT PRESENT ON LINUX)

    tty.c_cc[VTIME] = 10;    // Wait for up to 1s (10 deciseconds), returning as soon as any data is received.
    tty.c_cc[VMIN] = 0;

    // Set in/out baud rate to be 9600
    cfsetispeed(&tty, baudrate);
    cfsetospeed(&tty, baudrate);

    // Save tty settings, also checking for error
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        printf("Error %i from tcsetattr: %s\n", errno, strerror(errno));
        return 1;
    }

#else

    /*
          CS8     : 8n1 (8bit,no parity,1 stopbit)
          CLOCAL  : local connection, no modem contol
          CREAD   : enable receiving characters
     */
    options1.c_cflag = CS8 | CLOCAL | CREAD;

    /*
          IGNPAR  : ignore bytes with parity errors
          otherwise make device raw (no other input processing)
     */
    options1.c_iflag = 0; // IGNPAR;

    /* Raw output. */
    options1.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    options1.c_lflag = 0;

    options1.c_cc[VMIN]     = 0;   /* no blocking read */
    options1.c_cc[VTIME]    = 0;   /* inter-character timer unused */

    cfsetospeed(&options1, baudrate);
    cfsetispeed(&options1, baudrate);

    tcflush(fd, TCIFLUSH);
    tcsetattr(fd, TCSANOW, &options1);
#endif

    return 0;
}

static int PL_Connect(const char *path, int baudrate)
{
    M_ASSERT(plThread == nullptr);

    if (plThread || platform.fd != 0)
    {
        DBG_Printf(DBG_PROT, "device already connected %s\n", path);
        return 1;
    }

    platform.fd = open(path, O_RDWR | /*O_NONBLOCK |*/ O_NOCTTY);

    if (platform.fd < 0)
    {
#ifdef DECONZ_DEBUG_BUILD
        int err = errno;
        DBG_Printf(DBG_PROT, "failed to open device %s: %s\n", path, strerror(err));
#endif
        platform.fd = 0;
        return 0;
    }

    DBG_Printf(DBG_PROT, "connected to %s\n", path);

    if (baudrate == 0)
    {
        // TODO this is mostly guess wrong
        baudrate = B38400;

#ifdef __linux__
        struct stat sb;

        if (fstat(platform.fd, &sb) == 0)
        {
            /* major device number

                166 /dev/ttyACM0 (cdc acm, ConBee II)
                188 /dev/ttyUSB0 (serial, ConBee I)
                  4 /dev/serial0 (UART RaspBee II)
                204 /dev/serial1 (NOT the hardware UART)
            */
            int dev_major = sb.st_rdev >> 8;
#ifdef DECONZ_DEBUG_BUILD
            int dev_minor = sb.st_rdev & 0xFF;
            DBG_Printf(DBG_PROT, "[COM] major: %d, minor: %d\n", dev_major, dev_minor);
#endif
            if (dev_major == 166)
            {
                baudrate = B115200;
            }
        }
        else
#endif
        {
            if (strstr(path, "ACM") ||
                    strstr(path, "ConBee_II")) /* ConBee II Linux */
            {
                baudrate = B115200;
            }
            else if (strstr(path, "cu.usbmodemDE")) /* ConBee II macOS */
            {
                baudrate = B115200;
            }
        }
    }
    else if (baudrate == 115200)
    {
        baudrate = B115200;
    }
    else if (baudrate == 38400)
    {
        baudrate = B38400;
    }

    plSetupPort(platform.fd, baudrate);

    plThread = new PL_Thread;
    plThread->rx_a = 0;
    plThread->rx_b = 0;
    plThread->running = false;
    plThread->events = 0;
    plThread->th = std::thread(PL_Thread0);

    return 1;
}

static void PL_Disconnect()
{
    M_ASSERT(plThread);
    M_ASSERT(plThread->th.joinable());

    {
//        std::lock_guard<std::mutex> lock(plThread->mtx_fd);
        plThread->running = false;
    }

    plThread->th.join();
    delete plThread;
    plThread = nullptr;

    if (platform.fd != 0)
    {
        close(platform.fd);
        platform.fd = 0;
    }
}

static int PL_BytesToWrite()
{
    return 0;
}

static int PL_Read(void *buf, int maxsize)
{
    int nread = 0;
    unsigned char* data = (unsigned char*)buf;

    if (plThread && maxsize > 0)
    {
        for (;plThread->rx_a != plThread->rx_b && nread < maxsize;)
        {
            data[nread] = plThread->rxbuf[plThread->rx_a % TH_RX_BUFFER_SIZE];
            nread++;
            plThread->rx_a++;
        }
    }
    return nread;
}

static int PL_Write(const void *buf, int size)
{
    int n = 0;
    int err = 0;
    int err2 = 0;
    int max_loops = 8;
    int written = 0;
    M_ASSERT(platform.fd != 0);
    M_ASSERT(size > 0);

    uint8_t *dat = (uint8_t*)buf;
    using namespace std::chrono_literals;

    if (plThread && size > 0)
    {
        {
            do
            {
                err = 0;
                n = write(platform.fd, buf, size);

                if (n < 0)
                {
                    err = errno;
                    if (err != EINTR && err != EWOULDBLOCK)
                    {
                        DBG_Printf(DBG_ERROR, "[COM] write error: %s (%d)\n", strerror(err), err);
                    }
                    else
                    {
                        err = 0;
                        max_loops--;
                        DBG_Printf(DBG_ERROR, "[COM] write delay: %s, max_loops: %d\n", strerror(err), max_loops);
                        std::this_thread::sleep_for(2ms);
                    }
                }
                else
                {
                    M_ASSERT(n <= size);
                    size -= n;
                    dat += n;
                    written += n;
                    err2 = tcdrain(platform.fd); // flush written
                    if (err2 != 0)
                    {
                        DBG_Printf(DBG_ERROR, "[COM] tcdrain error: %s (%d)\n", strerror(err2), err2);
                    }
                }
            } while (err == 0 && size > 0 && max_loops > 0);
        }

        plThread->cv.notify_all();
    }

    return written;
}

static void PL_Poll()
{
    Com->readyRead();
}

#endif

SerialCom::~SerialCom()
{
    delete d;
    d = nullptr;
    Com = nullptr;
    ComPriv = nullptr;
}

int SerialCom::open(const QString &port, int baudrate)
{
    DBG_Assert(!port.isEmpty());
    if (port.isEmpty())
        return -1;

    M_ASSERT(d->state() == ComStateOff);
    if (d->state() != ComStateOff)
    {
        return -1;
    }

    d->port = port;

    if (d->open(baudrate) == 0)
    {
        M_ASSERT(d->pollTimerId == -1);
        d->setState(ComStateQueryBootloader);
        d->timer.start(500);
        return 0;
    }

    return -1;
}

int SerialCom::close(void)
{
    if (d->pollTimerId != -1)
    {
        killTimer(d->pollTimerId);
        d->pollTimerId = -1;
    }
    return d->close();
}

bool SerialCom::isOpen()
{
    return d->state() != ComStateOff;
}

bool SerialCom::isApplicationConnected()
{
    return d->state() == ComStateRxTx;
}

static void SER_ProcessEvents()
{
#ifndef USE_QSERIAL_PORT
    if (!plThread)
        return;

    std::lock_guard<std::mutex> rx_lock(plThread->mtx_rx);

    plThread->events &= ~TH0_EVENT_ID;

    if (plThread->events == 0)
        return;

    if (plThread->events & ERR_EVENT_ID)
    {
        Com->close();
        plThread->events = 0;
        return;
    }

    if (plThread->events & RX_EVENT_ID)
    {
        PL_Poll();

        // if (SER_Isc() == 0) // ???
        // {
        // }

        if (plThread->rx_a == plThread->rx_b)
        {
            plThread->events &= ~RX_EVENT_ID;
        }
        else
        {
            plThread->events |= TH0_EVENT_ID;
            emit Com->th0HasEvents();
        }
    }
#endif // ! USE_QSERIAL_PORT
}

void SerialCom::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == d->pollTimerId)
    {
        SER_ProcessEvents();
    }
}

/*
    buf[2]

    a = 0
    b = 0

    a       ==  b        // buffer empty
    a % sz  !=  b % sz   // non empty
    a % sz  ==  b % sz   // full

    :ins

    a = 0
    b = 1

    :ins

    a = 0
    b = 2

    :ins ... x 255

    a = 0
    b = 255



*/
static void TXQ_Test()
{
#ifdef DECONZ_DEBUG_BUILD
    unsigned a;

    TXQ_Init();

    // # 1
    M_ASSERT(TXQ_IsEmpty() == 1);
    M_ASSERT(TXQ_IsFull() == 0);

    // # 2
    TXQ_Push();
    M_ASSERT(TXQ_IsEmpty() == 0);
    M_ASSERT(TXQ_IsFull() == 0);

    // # 2
    TXQ_Push();
    M_ASSERT(TXQ_IsEmpty() == 0);
    M_ASSERT(TXQ_IsFull() == 0);

    // # 3
    a = TXQ_Pop();
    M_ASSERT(a == 0);
    a = TXQ_Pop();
    M_ASSERT(a == 1);
    M_ASSERT(TXQ_IsEmpty() == 1);
    M_ASSERT(TXQ_IsFull() == 0);

    // # 3
    TXQ_Init();
    for (unsigned i = 0; i < MAX_SEND_QUEUE_SIZE; i++)
    {
        TXQ_Push();
    }
    M_ASSERT(TXQ_IsEmpty() == 0);
    M_ASSERT(TXQ_IsFull() == 1);

    // # 4
    a = TXQ_Pop();
    M_ASSERT(TXQ_IsEmpty() == 0);
    M_ASSERT(TXQ_IsFull() == 0);
    M_ASSERT(a < MAX_SEND_QUEUE_SIZE);

#endif
}

// https://fgiesen.wordpress.com/2010/12/14/ring-buffers-and-queues/
static void TXQ_Init(void)
{
    sendPos = 0;
    sendEnd = 0;

    memset(&sendQueue[0], 0, sizeof(sendQueue));
}

static int TXQ_IsEmpty(void)
{
    return sendPos == sendEnd ? 1 : 0;
}

/*

    [0] A
    [1] B

    a = 0
    b = 2



*/
static int TXQ_IsFull(void)
{
    unsigned a = sendPos % MAX_SEND_QUEUE_SIZE;
    unsigned b = sendEnd % MAX_SEND_QUEUE_SIZE;

    if (sendPos != sendEnd && a == b)
    {
        return 1;
    }

    return 0;
}

static unsigned TXQ_Push(void)
{
    M_ASSERT(TXQ_IsFull() == 0);
    unsigned result = sendEnd;
    sendEnd++;
    return result % MAX_SEND_QUEUE_SIZE;
}

static unsigned TXQ_Pop(void)
{
    M_ASSERT(TXQ_IsEmpty() == 0);
    unsigned result = sendPos;
    sendPos++;
    return result % MAX_SEND_QUEUE_SIZE;
}

int SerialCom::send(zm_command *cmd)
{
    unsigned len = 0;

    {
#ifndef USE_QSERIAL_PORT
        if (!plThread)
            return -5;

        std::unique_lock<std::mutex> tx_lock(plThread->mtx_tx);
#endif

        if (TXQ_IsFull())
        {
            return -1;
        }

        if (!isApplicationConnected())
        {
            return -2;
        }

        unsigned ins = TXQ_Push();
        TrxBuffer &buf = sendQueue[ins];

        buf.length = zm_protocol_command2buffer(cmd, 0x1000, buf.data, sizeof(buf.data));
        len = buf.length;

    } // end: mtx_tx scope

    if (len > 0)
    {
#ifdef USE_QSERIAL_PORT
        d->tx();
#endif

#ifndef USE_QSERIAL_PORT

    {
//        std::unique_lock<std::mutex> fd_lock(plThread->mtx_fd);
        plThread->events |= TX_EVENT_ID;
    }
#endif
        return 0;
    }

    return -3;
}

void SerialCom::readyRead()
{
    if (d->rxBuffer.size() == d->rxWritePos)
    {
        DBG_Printf(DBG_ERROR, "[COM] rx buffer full\n");
        d->rx();
        return;
    }

    M_ASSERT(d->rxWritePos < d->rxBuffer.size());

    int max = int(d->rxBuffer.size() - d->rxWritePos);
    int nread = PL_Read(&d->rxBuffer[d->rxWritePos], (unsigned)max);

    M_ASSERT(nread <= max);

    DBG_Printf(DBG_PROT, "[COM] ready read max: %d bytes, nread: %d bytes\n", max, nread);

    M_ASSERT(nread <= 0 || (d->rxWritePos + nread) <= d->rxBuffer.size());

    if (nread <= 0)
    {

    }
    else if ((size_t(nread) + d->rxWritePos) > d->rxBuffer.size())
    {
        DBG_Assert("[COM] read over rx buffer");
    }
    else if ((size_t(nread) + d->rxWritePos) <= d->rxBuffer.size())
    {
        d->rxWritePos +=size_t(nread);
    }

    DBG_Assert(d->rxWritePos <= d->rxBuffer.size());

    if (d->comState == ComStateRxTx)
    {
        d->rx();
    }
    else if (d->comState == ComStateWaitBootloader)
    {
        d->checkBootloader();
        d->rx();
    }
}

void SerialCom::bytesWritten(qint64 bytes)
{
    Q_UNUSED(bytes);

    int toWrite = PL_BytesToWrite();

    if (toWrite == 0 && (sendPos % MAX_SEND_QUEUE_SIZE) != (sendEnd % MAX_SEND_QUEUE_SIZE))
    {
        d->tx();
    }
}

void SerialCom::timeout()
{
    if (d->comState == ComStateQueryBootloader)
    {
        d->queryBootloader();
    }
    else if (d->comState == ComStateWaitBootloader)
    {
        d->setState(ComStateRxTx);
        emit connected();
    }
}

void SerialCom::processTh0Events()
{
    SER_ProcessEvents();
}

#ifdef USE_QSERIAL_PORT
void SerialCom::handleError(QSerialPort::SerialPortError error)
{
    DBG_Printf(DBG_PROT, "[COM] serial port error: %d\n", int(error));

    d->timer.stop();

    switch (error)
    {
    case QSerialPort::WriteError: return;
    case QSerialPort::ReadError: return;

    default:
        break;
    }

    d->close();

    emit disconnected(deCONZ::DeviceDisconnectIoError);
}
#endif

int SerialComPrivate::open(int baudrate)
{
    baudrate = deCONZ::appArgumentNumeric("--baudrate", baudrate);

    TXQ_Init();

    rxBytes = 0;
    rxWritePos = 0;
    rxReadPos = 0;

    if (PL_Connect(qPrintable(port), baudrate) == 0)
    {
        return -1;
    }

    // init protocol
    protocol_init();
    protId = protocol_add(PROTO_RX | PROTO_TX | PROTO_FLAGGED | PROTO_TRACE,
                                  SER_Getc, SER_Isc, SER_Putc, SER_Flush, SER_Packet);
    protocol_set_buffer(protId, PROT_RxBuffer, sizeof(PROT_RxBuffer));
    return 0;
}

int SerialComPrivate::close()
{
    if (protId != PROTO_NO_PROTOCOL)
    {
        protocol_remove(protId);
        protId = PROTO_NO_PROTOCOL;
        protocol_exit();
    }

    sendEnd = 0;
    sendPos = 0;

    closeReason = deCONZ::DeviceDisconnectNormal;

    if (PL_IsConnected())
    {
        PL_Disconnect();
    }

    if (comState != ComStateOff)
    {
        setState(ComStateOff);
        emit q->disconnected(closeReason);
    }

    return 0;
}

int SerialComPrivate::rx()
{
#ifdef USE_QSERIAL_PORT
    // quirk to handle internal Qt bug
    if (serialPort->error() != QSerialPort::NoError)
    {
        //#ifdef QT_DEBUG
        //            fprintf(stderr, "[COM] error: %s\n", qPrintable(serialPort->errorString()));
        //#endif
        serialPort->clearError();
    }
#endif

    // RX
    while (SER_Isc())
    {
        protocol_receive(protId);
    }

    return 0;
}

int SerialComPrivate::tx()
{
    if (TXQ_IsEmpty() == 0)
    {
        unsigned a =  TXQ_Pop();
        TrxBuffer &buf = sendQueue[a];

        if (buf.length > 0)
        {
            protocol_send(protId, buf.data, buf.length);
#ifdef DBG_SERIAL
            DBG_Printf(DBG_WIRE, "\n");
#endif
        }
    }

    return 0;
}

void SerialComPrivate::flush()
{
    int length = int(txWritePos) - int(txReadPos);
    if (length <= 0 || txWritePos > txBuffer.size())
    {
        txReadPos = 0;
        txWritePos = 0;
        return;
    }

    M_ASSERT((txReadPos + length) < txBuffer.size());
    int nwrite = PL_Write(&txBuffer[txReadPos], length);

    // M_ASSERT(nwrite == length);
    if (nwrite > 0)
    {
        txReadPos += nwrite;
        int remaining = (int)txWritePos - (int)txReadPos;
        DBG_Printf(DBG_PROT, "[COM] written %d bytes, left %d\n", nwrite, remaining);

        if (txReadPos > txBuffer.size() || txReadPos >= txWritePos)
        {
            txReadPos = 0;
            txWritePos = 0;
        }
        else if (remaining != 0)
        {
            txReadPos = 0;
            txWritePos = 0;
            DBG_Printf(DBG_ERROR, "[COM] flush() remaining: %d bytes\n", remaining);
        }

        //M_ASSERT(remaining == 0);
    }
}

void SerialComPrivate::queryBootloader()
{
    // check if bootloader is active
    DBG_Printf(DBG_PROT, "[COM] check bootloader\n");

    setState(ComStateWaitBootloader);
    PL_Write("ID", 2);
    timer.start(1000);
}

void SerialComPrivate::setState(ComState nextState)
{
    if (comState != nextState)
    {
#ifdef DBG_SERIAL
        DBG_Printf(DBG_WIRE, "[COM] state: %d --> %d\n", comState, nextState);
#endif
        comState = nextState;
    }
}

ComState SerialComPrivate::state()
{
    return comState;
}

void SerialComPrivate::checkBootloader()
{
    M_ASSERT(rxWritePos < rxBuffer.size());

    for (size_t i = 0; i < rxWritePos; i++)
    {
        // Bootloader
        if (rxBuffer[i] == 'B' && i < rxBuffer.size() - 12)
        {
            if (memcmp(&rxBuffer[i], "Bootloader", 10) == 0)
            {
                emit q->bootloaderStarted();
//                rxBuffer[rxWritePos] = '\0';
//                DBG_Printf(DBG_INFO, "%s", &rxBuffer[0]);
            }
        }
    }
}

static char SER_Getc(void)
{
    M_ASSERT(ComPriv);

    if (ComPriv && ComPriv->rxReadPos < ComPriv->rxWritePos)
    {
        const char c = ComPriv->rxBuffer[ComPriv->rxReadPos++] & 0xFF;

        if (ComPriv->rxReadPos == ComPriv->rxWritePos)
        {
            ComPriv->rxReadPos = 0;
            ComPriv->rxWritePos = 0;
        }

#ifdef DBG_SERIAL
        if (DBG_IsEnabled(DBG_WIRE))
        {
            printf("%02X ", c & 0xFF);
        }
#endif
        return c;
    }

    return 0;
}

static char SER_Isc(void)
{
    if (ComPriv && ComPriv->rxReadPos < ComPriv->rxWritePos)
    {
        return 1;
    }

    return 0;
}

short SER_Putc(char c)
{
    if (!ComPriv)
    {
        return 0;
    }

    if (ComPriv->txWritePos < ComPriv->txBuffer.size())
    {
#ifdef DBG_SERIAL
        if (DBG_IsEnabled(DBG_WIRE))
        {
            printf("%02X ", c & 0xFF);
        }
#endif
        ComPriv->txBuffer[ComPriv->txWritePos++] = c & 0xFF;

        return 1;
    }

    return 0;
}

static void SER_Flush()
{
    if (ComPriv)
    {
        ComPriv->flush();
    }
}

#ifdef DBG_SERIAL
static unsigned char ascii[512];
#endif

static void SER_Packet(uint8_t *data, uint16_t length)
{
    if (data && length > 0)
    {
#ifdef DBG_SERIAL
        if (DBG_IsEnabled(DBG_PROT))
        {
            DBG_HexToAscii(data, length, ascii);
            DBG_Printf(DBG_PROT_L2, "[COM] rx: %s\n", ascii);
        }
#endif
        struct zm_command cmd;
        const auto ret = zm_protocol_buffer2command(data, length, &cmd);
        if (ret == ZM_PARSE_OK)
        {
            COM_OnPacket(&cmd);
        }
#ifdef DBG_SERIAL
        else
        {
            DBG_Printf(DBG_PROT, "[COM] failed to extract packet from frame, error: %d\n", int(ret));
            DBG_Flush();
        }
#endif
    }
}
