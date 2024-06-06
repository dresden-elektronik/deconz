/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QDir>
#include <QSsl>
#include <QSslKey>
#include <QFile>
#include <QFileSystemWatcher>
#include <QSettings>
#include "zm_http_server.h"
#include "zm_http_client.h"
#include "zm_https_client.h"
#include "zm_controller.h"
#include "zm_master.h"
#include "deconz/dbg_trace.h"
#include "deconz/http_client_handler.h"
#include "deconz/util.h"

#ifdef Q_OS_WIN
    #define HTTP_SERVER_PORT 80
#else
    #define HTTP_SERVER_PORT 8080
#endif

static deCONZ::HttpServer *httpInstance = nullptr;

namespace deCONZ {

class HttpServerPrivate
{
public:
    bool useHttps = false;
    bool useAppCache = false;
    QString serverRoot;
    uint16_t serverPort;

    std::vector<deCONZ::HttpClientHandler*> clientHandlers;
    std::vector<zmHttpClient::CacheItem> m_cache;
    QFileSystemWatcher *fsWatcher = nullptr;
};

HttpServer::HttpServer(QObject *parent) :
    QTcpServer(parent),
    d(new HttpServerPrivate)
{
    httpInstance = this;
    d->serverRoot = "/";
    connect(this, SIGNAL(newConnection()),
            this, SLOT(clientConnected()));

#if defined(QT_DEBUG)
    d->fsWatcher = new QFileSystemWatcher(this);
    connect(d->fsWatcher, SIGNAL(directoryChanged(QString)),
            this, SLOT(clearCache()));
    connect(d->fsWatcher, SIGNAL(fileChanged(QString)),
            this, SLOT(clearCache()));
    connect(d->fsWatcher, SIGNAL(directoryChanged(QString)),
            this, SLOT(updateFileWatcher()));
#endif

    d->serverPort = HTTP_SERVER_PORT;
    bool useAppCache = true;
    QString serverRoot;
    QString listenAddress("0.0.0.0");
    QString configPath = deCONZ::getStorageLocation(deCONZ::ConfigLocation);
    QSettings config(configPath, QSettings::IniFormat);

    bool ok = false;
    if (config.contains("http/appcache"))
    {
        useAppCache = config.value("http/appcache").toBool();
    }
    else
    {
        config.setValue("http/appcache", useAppCache);
    }

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

#ifdef Q_OS_LINUX
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
#endif // Q_OS_LINUX

    config.setValue("http/port", d->serverPort);

    serverRoot = deCONZ::appArgumentString("--http-root", "");

    if (serverRoot.isEmpty())
    {
#ifdef Q_OS_LINUX
        serverRoot = "/usr/share/deCONZ/webapp/";
#endif
#ifdef __APPLE__
        QDir dir(qApp->applicationDirPath());
        dir.cdUp();
        dir.cd("Resources");
        serverRoot = dir.path() + "/webapp/";
#endif
#ifdef Q_OS_WIN
        serverRoot = QCoreApplication::applicationDirPath() + QLatin1String("/plugins/de_web/");
#endif
    }

    if (!QFile::exists(serverRoot))
    {
        DBG_Printf(DBG_ERROR, "Server root directory %s doesn't exist\n", qPrintable(serverRoot));
    }


    setServerRoot(serverRoot);
    setUseAppCache(useAppCache);

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
}

HttpServer::~HttpServer()
{
    httpInstance = nullptr;
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

#if QT_VERSION < 0x050000

  void HttpServer::incomingConnection(int socketDescriptor)
#else
  void HttpServer::incomingConnection(qintptr socketDescriptor)
#endif
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
    //DBG_Assert(lowDelay == 1);

    for (auto *handler : d->clientHandlers)
    {
        sock->registerClientHandler(handler);
    }
}

#if 0
void HttpServer::handleHttpsClient(int socketDescriptor)
{
    zmHttpsClient *sock = new zmHttpsClient(this);
    if (sock->setSocketDescriptor(socketDescriptor))
    {
//        sock->addCaCertificates("TODO.cert");
        QString privKeyFile = "server.key";
        QString localCertFile = "server.csr";

        sock->setLocalCertificate(localCertFile);

        QFile file(privKeyFile);

         file.open(QIODevice::ReadOnly);
         if (!file.isOpen()) {
             DBG_Printf(DBG_ERROR, "server.key not found\n");
            sock->disconnectFromHost();
            return;
         }

        QSslKey key(&file, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey, "");
        sock->setPrivateKey(key);
        sock->startServerEncryption();
        if (!sock->waitForEncrypted(2000))
        {
            DBG_Printf(DBG_ERROR, "SSL: %s\n", qPrintable(sock->errorString()));
        }
        else
        {
            DBG_Printf(DBG_ERROR, "SSL OK: %s\n", qPrintable(sock->peerAddress().toString()));
        }
    }
    else
    {
        DBG_Printf(DBG_ERROR, "setSocketDescriptor failed: %s\n", qPrintable(sock->errorString()));
    }
    //addPendingConnection(sock);
    emit newConnection();

    std::list<deCONZ::HttpClientHandler*>::iterator i = d->clientHandlers.begin();
    std::list<deCONZ::HttpClientHandler*>::iterator end = d->clientHandlers.end();

    for (; i != end; ++i)
    {
        sock->registerClientHandler(*i);
    }

// TODO: generate self signed certificate http://www.akadia.com/services/ssh_test_certificate.html
}
#endif

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

void HttpServer::setUseAppCache(bool useAppCache)
{
    d->useAppCache = useAppCache;
}

bool HttpServer::useAppCache() const
{
    return d->useAppCache;
}

void HttpServer::clientConnected()
{
    QTcpSocket *sock = nextPendingConnection();

    if (sock)
    {
        connect(sock, SIGNAL(disconnected()),
                sock, SLOT(deleteLater()));

//        DBG_Printf(DBG_INFO, "HTTP client connected %s:%u\n", qPrintable(sock->peerAddress().toString()), sock->peerPort());
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
#ifndef ARCH_ARM
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    if (!d->fsWatcher)
    {
        return;
    }
    QDir dir(d->serverRoot);
    QStringList filter;
    filter << "*.html" << "*.appcache";
    QStringList files = dir.entryList(filter);

    auto i = files.constBegin();
    const auto end = files.constEnd();

    for (; i != end; ++i)
    {
        d->fsWatcher->addPath(dir.absolutePath() + "/" + *i);
    }
#endif
#endif // ! ARCH_ARM
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
