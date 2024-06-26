/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_HTTP_CLIENT_H
#define ZM_HTTP_CLIENT_H

#include <array>
#include <QTcpSocket>
#include "deconz/qhttprequest_compat.h"

namespace deCONZ {
    class HttpClientHandler;
}

class zmHttpClient : public QTcpSocket
{
    Q_OBJECT

public:
    enum Constants
    {
        MaxHandlers = 2
    };

    struct CacheItem
    {
        QString path;
        QString etag;
        QString lastModified;
        QByteArray content;
        int fileSize = 0;
    };

    explicit zmHttpClient(std::vector<CacheItem> &cache, QObject *parent = nullptr);
    ~zmHttpClient();
    int registerClientHandler(deCONZ::HttpClientHandler *handler);

public slots:
    void detachHandlers();
    void handleHttpRequest();
    int handleHttpFileRequest(const QHttpRequestHeader &hdr);

private slots:
    void handlerDeleted();

private:
    enum ClientState
    {
        ClientIdle,
        ClientRecvHeader,
        ClientRecvContent,
        ClientSendSetColor,
        ClientSendGetColor
    };

    QString m_serverRoot;
    ClientState m_clientState;
    QHttpRequestHeader m_hdr;
    std::vector<char> m_headerBuf;
    std::array<deCONZ::HttpClientHandler*, MaxHandlers> m_handlers{};
    std::vector<CacheItem> &m_cache;
};

#endif // ZM_HTTP_CLIENT_H
