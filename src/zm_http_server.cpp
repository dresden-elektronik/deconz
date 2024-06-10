/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
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
#include "deconz/util.h"

// enabled only during tests for new SSL implementation
// #define TEST_SSL_IMPL

#define NCLIENT_HANDLE_INDEX_MASK 0xFFFF
#define NCLIENT_HANDLE_EVOLUTION_SHIFT 17
#define NCLIENT_HANDLE_IS_SSL_FLAG 0x10000 // bit 17


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
};

class HttpServerPrivate
{
public:
    bool useHttps = false;
    QString serverRoot;
    uint16_t serverPort;
    N_SslSocket httpsSock;

    std::vector<NClient> clients;

    std::vector<deCONZ::HttpClientHandler*> clientHandlers;
    std::vector<zmHttpClient::CacheItem> m_cache;
    QFileSystemWatcher *fsWatcher = nullptr;
};

int HttpSend(unsigned handle, const void *buf, unsigned len)
{
    U_ASSERT(buf);
    U_ASSERT(len);
    U_ASSERT(privHttpInstance);
    deCONZ::HttpServerPrivate *d = privHttpInstance;

    unsigned index;

    if (!buf || len == 0)
        return 0;

    for (index = 0; index < d->clients.size(); index++)
    {
        if (d->clients[index].handle == handle)
            break;
    }
    if (d->clients.size() <= index)
        return 0;

    NClient &cli = d->clients[index];
    size_t beg = cli.writeBuf.size();

    cli.writeBuf.resize(beg + len);
    U_ASSERT(beg + len <= cli.writeBuf.size());
    U_memcpy(&cli.writeBuf[beg], buf, len);

    return 1;
}

HttpServer::HttpServer(QObject *parent) :
    QTcpServer(parent),
    d(new HttpServerPrivate)
{
    httpInstance = this;
    privHttpInstance = d;

    N_SslInit();

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

        if (N_SslServerInit(&d->httpsSock, &addr, port))
        {
        }
    }
#endif
}

HttpServer::~HttpServer()
{
    N_TcpClose(&d->httpsSock.tcp);
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
    zmHttpClient *sock = new zmHttpClient(d->m_cache, this);
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

#ifdef TEST_SSL_IMPL
    static N_SslSocket clientSock;

    if (N_SslAccept(&d->httpsSock, &clientSock))
    {
        DBG_Printf(DBG_INFO, "TCP accept\n");

        NClient cli;

        if (++handleEvolution >= 0x7FFF) // 15-bit counter
            handleEvolution = 0;

        // handle: 15-bit evolution | SSL flag | 16-bit index
        cli.handle = handleEvolution;
        cli.handle <<= NCLIENT_HANDLE_EVOLUTION_SHIFT;
        cli.handle |= NCLIENT_HANDLE_IS_SSL_FLAG;
        cli.handle += d->clients.size();

        U_memcpy(&cli.sock, &clientSock, sizeof(clientSock));

        d->clients.push_back(cli);
    }

    if (d->clients.empty())
        return;

    if (d->clients.size() <= clientIter)
        clientIter = 0;

    NClient &cli = d->clients[clientIter];
    clientIter++;

    if (cli.handle & NCLIENT_HANDLE_IS_SSL_FLAG)
    {
        if (N_SslHandshake(&cli.sock) == 0)
            return;

        if (N_SslCanRead(&cli.sock))
        {
            char buf[2048];
            int n = N_SslRead(&cli.sock, buf, sizeof(buf) - 1);
            if (n == 0)
            {
                DBG_Printf(DBG_INFO, "TCP done\n");
            }
            else if (n > 0 && (unsigned)n < sizeof(buf))
            {
                buf[n] = '\0';
                size_t beg = cli.readBuf.size();

                cli.readBuf.reserve(beg + n + 1);
                cli.readBuf.resize(beg + n);
                U_ASSERT(beg + n + 1 <= cli.readBuf.capacity());
                U_memcpy(&cli.readBuf[beg], buf, n + 1);

                DBG_Printf(DBG_INFO, "%s\n", buf);


                const char *dummyRsp =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Length: 14\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "Hello deCONZ\r\n";

                HttpSend(cli.handle, dummyRsp, qstrlen(dummyRsp));
            }
        }

        if (cli.writePos < cli.writeBuf.size())
        {
            unsigned len = cli.writeBuf.size() - cli.writePos;
            int n = N_SslWrite(&cli.sock, &cli.writeBuf[cli.writePos], len);

            if (n > 0)
            {
                DBG_Printf(DBG_INFO, "TCP written %d bytes\n", n);
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
#endif // TEST_SSL_IMPL
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
