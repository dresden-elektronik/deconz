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

    explicit zmHttpClient(const QString &serverRoot, std::vector<CacheItem> &cache, unsigned cliHandle, QObject *parent = nullptr);
    ~zmHttpClient();
    int registerClientHandler(deCONZ::HttpClientHandler *handler);
    qint64 bytesAvailable() const override;
    void close() override;

public slots:
    void rx(const uint8_t *buf, unsigned size);
    void detachHandlers();
    void handleHttpRequest();
    int handleHttpFileRequest(const QHttpRequestHeader &hdr);

private slots:
    void handlerDeleted();

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;

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
    std::vector<uint8_t> m_rxBuf;
    std::array<deCONZ::HttpClientHandler*, MaxHandlers> m_handlers{};
    std::vector<CacheItem> &m_cache;
    unsigned m_cliHandle = 0;
};

#endif // ZM_HTTP_CLIENT_H
