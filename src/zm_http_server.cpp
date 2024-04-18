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

namespace deCONZ {

class HttpServerPrivate
{
public:
    bool useHttps = false;
    bool useAppCache = false;
    QString serverRoot;
    std::vector<deCONZ::HttpClientHandler*> clientHandlers;
    std::vector<zmHttpClient::CacheItem> m_cache;
    QFileSystemWatcher *fsWatcher = nullptr;
};

HttpServer::HttpServer(QObject *parent) :
    QTcpServer(parent),
    d(new HttpServerPrivate)
{
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
}

HttpServer::~HttpServer()
{
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

} // namespace deCONZ
