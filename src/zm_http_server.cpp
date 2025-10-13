/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QSettings>
#include "zm_http_server.h"
#include "zm_http_client.h"
#include "zm_https_client.h"
#include "deconz/dbg_trace.h"
#include "deconz/http_client_handler.h"
#include "deconz/n_ssl.h"
#include "deconz/n_tcp.h"
#include "deconz/u_assert.h"
#include "deconz/u_memory.h"
#include "deconz/u_threads.h"
#include "deconz/util.h"

// enabled only during tests for new SSL implementation
#define TEST_SSL_IMPL

#define NCLIENT_HANDLE_INDEX_MASK 0xFFFF
#define NCLIENT_HANDLE_EVOLUTION_SHIFT 17
#define NCLIENT_HANDLE_IS_SSL_FLAG 0x10000 // bit 17

#define TH_MSG_SHUTDOWN    1
#define TH_MSG_CLIENT_NEW  10
#define TH_MSG_CLIENT_TX   11
#define TH_MSG_CLIENT_RX   12
#define TH_MSG_CLIENT_CLOSED   13

#define TH_QUEUE_SIZE (1<<21) // 2 MB


#ifdef PL_WINDOWS
    #define HTTP_SERVER_PORT 80
#else
    #define HTTP_SERVER_PORT 8080
#endif

static uint16_t handleEvolution;
static unsigned clientIter;
static deCONZ::HttpServer *httpInstance = nullptr;
static deCONZ::HttpServerPrivate *privHttpInstance = nullptr;

namespace deCONZ {

/*! The NClient can be either HTTP or HTTPS.
 *
 * A HTTP client only uses sock.tcp, and HTTPS the whole sock N_SslSocket object.
 */
struct NClient
{
    unsigned handle = 0;
    unsigned writePos = 0;
    std::vector<char> writeBuf;
    std::vector<char> readBuf;
    N_SslSocket sock;
    zmHttpClient *httpClient = nullptr;
};

using queue_word = uint32_t;

class HttpServerPrivate
{
public:
    HttpServer *q;
    bool useHttps = false;
    QString serverRoot;
    uint16_t serverPort;
    N_SslSocket httpsSock;

    std::vector<deCONZ::HttpClientHandler*> clientHandlers;
    std::vector<zmHttpClient::CacheItem> m_cache;
    QFileSystemWatcher *fsWatcher = nullptr;
    U_Thread thread;
    U_Mutex mutex;
    // following are protected by <mutex>
    unsigned qrp = 0;
    unsigned qwp = 0;
    // the qinout ring buffer is used for both: send and receive messages
    // to and from the thread.
    std::array<queue_word, TH_QUEUE_SIZE> qinout;
    std::vector<NClient> clients;

    NClient *getClientForHandle(unsigned cliHandle);
};

NClient *HttpServerPrivate::getClientForHandle(unsigned int cliHandle)
{
    for (size_t i = 0; i < clients.size(); i++)
    {
        if (clients[i].handle == cliHandle)
            return &clients[i];
    }

    return nullptr;
}

static void queuePut(HttpServerPrivate *d, queue_word word)
{
    const unsigned wp = (d->qwp + 1) & (TH_QUEUE_SIZE - 1);
    const unsigned rp = d->qrp & (TH_QUEUE_SIZE - 1);

    if (wp != rp)
    {
        d->qinout[d->qwp & (TH_QUEUE_SIZE - 1)] = word;
        d->qwp++;
    }
}

static queue_word queueGet(HttpServerPrivate *d)
{
    if (d->qrp != d->qwp)
    {
        unsigned rp = d->qrp;
        d->qrp++;
        return d->qinout[rp & (TH_QUEUE_SIZE - 1)];
    }

    return 0;
}

static queue_word queuePeek(HttpServerPrivate *d)
{
    if (d->qrp != d->qwp)
    {
        return d->qinout[d->qrp & (TH_QUEUE_SIZE - 1)];
    }

    return 0;
}

static bool queueIsEmpty(HttpServerPrivate *d)
{
    return (d->qrp == d->qwp);
}

/*
free_space = capacity - 1 - used_space
used_space = (head - tail + capacity) % capacity
*/
static unsigned queueFreeWords(HttpServerPrivate *d)
{
    const auto usedSpace = (d->qwp - d->qrp + TH_QUEUE_SIZE) % TH_QUEUE_SIZE;
    const auto freeSpace = TH_QUEUE_SIZE - 1 - usedSpace;
    return freeSpace;
}

static bool queueSpaceForWords(HttpServerPrivate *d, unsigned count)
{
    return count <= queueFreeWords(d);
}

/*
 * This thread handles: accept, read and write of TCP/SSL sockets.
 */
static void httpThreadFunc(void *arg)
{
    HttpServerPrivate *d = static_cast<HttpServerPrivate*>(arg);

    U_thread_set_name(&d->thread, "tcp/http");
    bool running = true;

    for (;running;)
    {
        bool msgOut = 0;
        if (U_thread_mutex_trylock(&d->mutex))
        {
            for (;!queueIsEmpty(d);)
            {
                auto msg = queuePeek(d);
                if (msg == TH_MSG_SHUTDOWN)
                {
                    running = false;
                    d->qrp = d->qwp = 0;
                    break;
                }
                else
                {
                    break;
                }
            }

            if (running)
            {
                // -k to accept self signed certificate
                // curl --http1.1 -k -vv https://192.168.178.32/api/config
                if (queueSpaceForWords(d, 2))
                {
                    NClient cli;

                    if (N_SslAccept(&d->httpsSock, &cli.sock))
                    {
                        DBG_Printf(DBG_INFO, "TCP accept\n");

                        if (++handleEvolution >= 0x7FFF) // 15-bit counter
                            handleEvolution = 0;

                        // handle: 15-bit evolution | SSL flag | 16-bit index
                        cli.handle = handleEvolution;
                        cli.handle <<= NCLIENT_HANDLE_EVOLUTION_SHIFT;
                        cli.handle |= NCLIENT_HANDLE_IS_SSL_FLAG;
                        cli.handle += d->clients.size();

                        d->clients.push_back(cli);

                        queuePut(d, TH_MSG_CLIENT_NEW);
                        queuePut(d, cli.handle);
                        msgOut = true;
                    }
                }

                if (!d->clients.empty())
                {
                    if (d->clients.size() <= clientIter)
                        clientIter = 0;

                    NClient &cli = d->clients[clientIter];
                    clientIter++;

                    if (cli.handle & NCLIENT_HANDLE_IS_SSL_FLAG)
                    {
                        if (N_SslHandshake(&cli.sock) != 0)
                        {
                            const unsigned minWordsFree = 32; // ensure important messages can be delivered
                            if (queueSpaceForWords(d, minWordsFree) && N_SslCanRead(&cli.sock))
                            {
                                char buf[2048];
                                const int wordsz = sizeof(queue_word);
                                auto nfree = queueFreeWords(d) * wordsz;

                                nfree -= 3 * wordsz; // msg header
                                if ((sizeof(buf) - 1) < nfree)
                                    nfree = sizeof(buf) - 1;

                                int n = N_SslRead(&cli.sock, buf, nfree);
                                if (n == 0)
                                {
                                    queuePut(d, TH_MSG_CLIENT_CLOSED);
                                    queuePut(d, cli.handle);
                                    msgOut = true;
                                }
                                else if (n > 0 && (unsigned)n <= nfree)
                                {
                                    buf[n] = '\0';
                                    queuePut(d, TH_MSG_CLIENT_RX);
                                    queuePut(d, cli.handle);
                                    queuePut(d, (queue_word)n);

                                    int nwords = (n + 3) / wordsz;

                                    int i;

                                    for (i = 0; i < nwords; i++)
                                    {
                                        queue_word word;
                                        U_memcpy(&word, &buf[i * wordsz], wordsz);
                                        queuePut(d, word);
                                    }

                                    msgOut = true;
                                }
                            }

                            if (cli.writePos < cli.writeBuf.size())
                            {
                                unsigned len = cli.writeBuf.size() - cli.writePos;
                                int n = N_SslWrite(&cli.sock, &cli.writeBuf[cli.writePos], len);

                                if (n > 0)
                                {
                                    DBG_Printf(DBG_INFO, "TCP written %d bytes, cliHandle: %u\n", n, cli.handle);
                                    cli.writePos += n;

                                    if (cli.writeBuf.size() <= cli.writePos)
                                    {
                                        // all done
                                        cli.writePos = 0;
                                        cli.writeBuf.clear();
                                    }
                                }
                            }
                        }
                    }
                }
            }

            U_thread_mutex_unlock(&d->mutex);

            if (msgOut)
            {
                emit d->q->threadMessage();
            }
        }

        U_thread_msleep(5);
    }

    U_thread_exit(0);
}

int HttpSend(unsigned handle, const void *buf, unsigned len)
{
    U_ASSERT(buf);
    U_ASSERT(len);
    U_ASSERT(privHttpInstance);
    deCONZ::HttpServerPrivate *d = privHttpInstance;

    unsigned index;
    int ret = 0;

    if (!buf || len == 0)
        return ret;

    // NOTE: This is called from within rx() so the mutex is already locked.

    for (index = 0; index < d->clients.size(); index++)
    {
        if (d->clients[index].handle == handle)
            break;
    }

    if (index < d->clients.size())
    {
        NClient &cli = d->clients[index];
        size_t beg = cli.writeBuf.size();

        cli.writeBuf.resize(beg + len);
        U_ASSERT(beg + len <= cli.writeBuf.size());
        U_memcpy(&cli.writeBuf[beg], buf, len);

        ret = len;
    }

    return ret;
}

HttpServer::HttpServer(QObject *parent) :
    QTcpServer(parent),
    d(new HttpServerPrivate)
{
    d->q = this;
    httpInstance = this;
    privHttpInstance = d;

    U_thread_mutex_init(&d->mutex);
    N_SslInit();

    connect(this, &HttpServer::threadMessage, this, &HttpServer::processClients, Qt::QueuedConnection);

    d->serverRoot = "/";
    connect(this, SIGNAL(newConnection()),
            this, SLOT(clientConnected()));

#ifdef DECONZ_DEBUG_BUILD
    d->fsWatcher = new QFileSystemWatcher(this);
    connect(d->fsWatcher, SIGNAL(directoryChanged(QString)),
            this, SLOT(clearCache()));
    connect(d->fsWatcher, SIGNAL(fileChanged(QString)),
            this, SLOT(clearCache()));
    connect(d->fsWatcher, SIGNAL(directoryChanged(QString)),
            this, SLOT(updateFileWatcher()));
#endif

    d->serverPort = HTTP_SERVER_PORT;

    QString serverRoot;
    QString listenAddress("0.0.0.0");
    QString configPath = deCONZ::getStorageLocation(deCONZ::ConfigLocation);
    QSettings config(configPath, QSettings::IniFormat);

    bool ok = false;

    if (config.contains("http/port"))
    {
        d->serverPort = config.value("http/port").toUInt(&ok);
    }

    if (config.contains("http/listen"))
    {
        listenAddress = config.value("http/listen", "0.0.0.0").toString();
    }

    listenAddress = deCONZ::appArgumentString("--http-listen", listenAddress);

    if (!ok)
    {
        d->serverPort = HTTP_SERVER_PORT;
    }

    d->serverPort = deCONZ::appArgumentNumeric("--http-port", d->serverPort);

#ifdef PL_LINUX
    if (d->serverPort <= 1024)
    {
        // NOTE: use setcap to enable ports below 1024 on the command line:
        // setcap cap_net_bind_service=+ep /usr/bin/deCONZ

        // check if this process is allowed to use privileged ports
#ifdef USE_LIBCAP
        bool changePort = true;
        cap_t caps = cap_get_proc();

        if (caps != NULL)
        {
            cap_flag_value_t effective;
            cap_flag_value_t permitted;

            cap_get_flag(caps, CAP_NET_BIND_SERVICE, CAP_EFFECTIVE, &effective);
            cap_get_flag(caps, CAP_NET_BIND_SERVICE, CAP_PERMITTED, &permitted);

            if ((effective == CAP_SET) || (permitted == CAP_SET))
            {
                changePort = false;
            }

            cap_free(caps);
        }
#else
        bool changePort = false; // assume it works
#endif // USE_LIBCAP

        if (changePort)
        {
            DBG_Printf(DBG_INFO, "HTTP server at port %u not allowed, use port %u instead\n", d->serverPort, HTTP_SERVER_PORT);
            d->serverPort = HTTP_SERVER_PORT;
        }
    }
#endif // PL_LINUX

    config.setValue("http/port", d->serverPort);

    serverRoot = deCONZ::appArgumentString("--http-root", "");

    if (serverRoot.isEmpty())
    {
#ifdef PL_LINUX
        serverRoot = "/usr/share/deCONZ/webapp/";
#endif
#ifdef __APPLE__
        QDir dir(qApp->applicationDirPath());
        dir.cdUp();
        dir.cd("Resources");
        serverRoot = dir.path() + "/webapp/";
#endif
#ifdef PL_WINDOWS
        serverRoot = QCoreApplication::applicationDirPath() + QLatin1String("/plugins/de_web/");
#endif
    }

    if (!QFile::exists(serverRoot))
    {
        DBG_Printf(DBG_ERROR, "Server root directory %s doesn't exist\n", qPrintable(serverRoot));
    }

    setServerRoot(serverRoot);

    std::vector<quint16> ports;
    ports.push_back(d->serverPort);
    ports.push_back(80);
    ports.push_back(8080);
    ports.push_back(8090);
    ports.push_back(9042);

    for (size_t i = 0; i < ports.size(); i++)
    {
        if (listen(QHostAddress(listenAddress), ports[i]))
        {
            d->serverPort = serverPort();
            DBG_Printf(DBG_INFO, "HTTP Server listen on address %s, port: %u, root: %s\n", qPrintable(listenAddress), serverPort(), qPrintable(serverRoot));
            break;
        }
        else
        {
            DBG_Printf(DBG_ERROR, "HTTP Server listen on address %s, port: %u error: %s\n", qPrintable(listenAddress), ports[i], qPrintable(errorString()));
        }
    }

    if (!isListening())
    {
        DBG_Printf(DBG_ERROR, "HTTP Server failed to start\n");
    }

    U_memset(&d->httpsSock, 0, sizeof(d->httpsSock));

    U_thread_create(&d->thread, httpThreadFunc, d);

#if 0 // TODO this is only a local test setup for new TCP implementation
    {
        if (N_TcpInit(&d->httpsSock.tcp, N_AF_IPV6))
        {
            N_Address addr;
            addr.af = N_AF_IPV6;

            if (N_TcpBind(&d->httpsSock.tcp, &addr, 6655))
            {
                if (N_TcpListen(&d->httpsSock.tcp, 10))
                {
                }
            }
        }
    }
#endif

#ifdef TEST_SSL_IMPL
    {
        N_Address addr{};
        addr.af = N_AF_IPV6;
        uint16_t port = 6655;

        const auto certPath = deCONZ::getStorageLocation(deCONZ::ApplicationsDataLocation).toStdString();

        if (N_SslServerInit(&d->httpsSock, &addr, port, certPath.c_str()))
        {
        }
    }
#endif
}

HttpServer::~HttpServer()
{
    // shutdown thread
    U_thread_mutex_lock(&d->mutex);
    d->qrp = d->qwp = 0;
    queuePut(d, TH_MSG_SHUTDOWN);
    U_thread_mutex_unlock(&d->mutex);
    U_thread_join(&d->thread);

    N_TcpClose(&d->httpsSock.tcp);
    U_thread_mutex_destroy(&d->mutex);
    httpInstance = nullptr;
    privHttpInstance = nullptr;
    delete d;
    d = 0;
}

int HttpServer::registerHttpClientHandler(HttpClientHandler *handler)
{
    if (!handler)
    {
        return -1;
    }

    const auto it = std::find(d->clientHandlers.cbegin(), d->clientHandlers.cend(), handler);

    // already registered?
    if (it != d->clientHandlers.end())
    {
        return -1;
    }

    // append to list
    d->clientHandlers.push_back(handler);
    return 0;
}

void HttpServer::incomingConnection(qintptr socketDescriptor)
{
    if (d->useHttps)
    {
        //handleHttpsClient(socketDescriptor);
    }
    else
    {
        handleHttpClient(socketDescriptor);
    }
}

void HttpServer::handleHttpClient(int socketDescriptor)
{
    unsigned cliHandle = 0; // TODO(mpi)
    zmHttpClient *sock = new zmHttpClient(serverRoot(), d->m_cache, cliHandle, this);
    sock->setSocketDescriptor(socketDescriptor);
    addPendingConnection(sock);
    emit newConnection();

    sock->setSocketOption(QTcpSocket::LowDelayOption, 1);
    int lowDelay = sock->socketOption(QTcpSocket::LowDelayOption).toInt();

    for (auto *handler : d->clientHandlers)
    {
        sock->registerClientHandler(handler);
    }
}

void HttpServer::setServerRoot(const QString &root)
{
    DBG_Assert(!root.isEmpty());
    d->serverRoot = root;
    if (d->fsWatcher)
    {
        QDir dir(root);
        d->fsWatcher->addPath(dir.absolutePath());
        if (QFile::exists(dir.absolutePath() + "-src"))
        {
            d->fsWatcher->addPath(dir.absolutePath() + "-src");
        }
        updateFileWatcher();
    }
}

const QString &HttpServer::serverRoot() const
{
    return d->serverRoot;
}

void HttpServer::processClients()
{
#if 0
    N_TcpSocket client;

    if (N_TcpCanRead(&d->httpsSock.tcp))
    {
        if (N_TcpAccept(&d->httpsSock.tcp, &client))
        {
            const char *dummyRsp =
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 12\r\n"
                "Connection: close\r\n"
                "\r\n"
                "Hello deCONZ";
            N_TcpWrite(&client, dummyRsp, qstrlen(dummyRsp));

            char dummyBuf[128];

            for (int wait = 0; wait < 5000;) /* stupid way to wait until peer closes connection, TODO timeout */
            {
                if (N_TcpCanRead(&client) == 0)
                {
                    wait++;
                    continue;
                }

                if (N_TcpRead(&client, &dummyBuf[0], sizeof(dummyBuf)) <= 0)
                    break;
            }
            N_TcpClose(&client);
        }
    }
#endif

    if (U_thread_mutex_lock(&d->mutex))
    {
        for (;!queueIsEmpty(d);)
        {
            auto msg = queuePeek(d);

            if (msg == TH_MSG_CLIENT_RX)
            {
                queueGet(d); // msg was for us
                auto cliHandle = queueGet(d);
                auto nbytes = queueGet(d);

                int wordsz = sizeof(queue_word);
                int nwords = (nbytes + 3) / wordsz;

                int i;
                uint8_t buf[2048 + 8];
                for (i = 0; i < nwords; i++)
                {
                    queue_word w = queueGet(d);
                    U_memcpy(&buf[i * wordsz], &w, wordsz);
                }
                buf[nbytes] = '\0';

                NClient *cli = d->getClientForHandle(cliHandle);
                if (cli && nbytes)
                {
                    U_ASSERT(cli->httpClient);
                    cli->httpClient->rx(buf, nbytes);
                }
            }
            else if (msg == TH_MSG_CLIENT_NEW)
            {
                queueGet(d); // msg was for us
                auto cliHandle = queueGet(d);

                NClient *cli = d->getClientForHandle(cliHandle);
                if (cli)
                {
                    U_ASSERT(cli->httpClient == nullptr);

                    cli->httpClient = new zmHttpClient(serverRoot(), d->m_cache, cliHandle, this);
                    connect(cli->httpClient, &zmHttpClient::destroyed, d->q, &HttpServer::clientDeleted);

                    for (auto *handler : d->clientHandlers)
                    {
                        cli->httpClient->registerClientHandler(handler);
                    }
                }

                DBG_Printf(DBG_INFO, "new tcp/ssl client: handle: %u\n", cliHandle);
            }
            else if (msg == TH_MSG_CLIENT_CLOSED)
            {
                queueGet(d); // msg was for us
                auto cliHandle = queueGet(d);

                NClient *cli = d->getClientForHandle(cliHandle);
                if (cli && cli->httpClient)
                {
                    DBG_Printf(DBG_INFO, "TCP done, cliHandle: %u\n", cli->handle);
                    emit cli->httpClient->disconnected();
                    cli->httpClient->deleteLater();
                    cli->httpClient = nullptr;
                    // remove
                    *cli = d->clients.back();
                    d->clients.pop_back();
                }
            }
            else
            {
                break;
            }
        }

        U_thread_mutex_unlock(&d->mutex);
    }
}

void HttpServer::clientConnected()
{
    QTcpSocket *sock = nextPendingConnection();

    if (sock)
    {
        connect(sock, SIGNAL(disconnected()),
                sock, SLOT(deleteLater()));
    }
}

void HttpServer::clientDeleted(QObject *obj)
{
    U_thread_mutex_lock(&d->mutex);

    for (auto &cli : d->clients)
    {
        if (cli.httpClient == static_cast<zmHttpClient*>(obj))
        {
            N_SslClose(&cli.sock);
            cli = d->clients.back();
            d->clients.pop_back();
            break;
        }
    }

    U_thread_mutex_unlock(&d->mutex);
}

void HttpServer::clearCache()
{
    if (!d->m_cache.empty())
    {
        DBG_Printf(DBG_INFO, "HTTP clear server cache\n");
        d->m_cache.clear();
    }
}

void HttpServer::updateFileWatcher()
{
#ifdef DECONZ_DEBUG_BUILD
#ifndef ARCH_ARM
#if defined(PL_WINDOWS) || defined(PL_LINUX)
    if (!d->fsWatcher)
    {
        return;
    }
    QDir dir(d->serverRoot);
    QStringList filter;
    filter << "*.html";
    QStringList files = dir.entryList(filter);

    auto i = files.constBegin();
    const auto end = files.constEnd();

    for (; i != end; ++i)
    {
        d->fsWatcher->addPath(dir.absolutePath() + "/" + *i);
    }
#endif
#endif // ! ARCH_ARM
#endif // DECONZ_DEBUG_BUILD
}

uint16_t httpServerPort()
{
    if (httpInstance)
    {
        return httpInstance->serverPort();
    }

    return 0;
}

QString httpServerRoot()
{
    if (httpInstance)
    {
        return httpInstance->serverRoot();
    }

    return QString();
}

int registerHttpClientHandler(deCONZ::HttpClientHandler *handler)
{
    if (httpInstance && handler)
    {
        return httpInstance->registerHttpClientHandler(handler);
    }

    return -1;
}

} // namespace deCONZ
