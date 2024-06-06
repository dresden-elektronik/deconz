/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_HTTP_SERVER_H
#define ZM_HTTP_SERVER_H

#include <QTcpServer>

extern const char *HttpStatusOk;
extern const char *HttpStatusAccepted;
extern const char *HttpStatusBadRequest;
extern const char *HttpStatusForbidden;
extern const char *HttpStatusNotFound;
extern const char *HttpContentHtml;
extern const char *HttpContentCss;
extern const char *HttpContentJson;
extern const char *HttpContentJS;
extern const char *HttpContentPNG;
extern const char *HttpContentJPG;
extern const char *HttpContentSVG;
extern const char *HttpContentXML;
extern const char *HttpContentAppCache;

namespace deCONZ
{

class HttpServerPrivate;
class HttpClientHandler;

class HttpServer : public QTcpServer
{
    Q_OBJECT

public:
    explicit HttpServer(QObject *parent = 0);
    ~HttpServer();
    int registerHttpClientHandler(deCONZ::HttpClientHandler *handler);
    void incomingConnection(qintptr socketDescriptor);
    void handleHttpClient(int socketDescriptor);
    //void handleHttpsClient(int socketDescriptor);
    void setServerRoot(const QString &root);
    const QString &serverRoot() const;
    void processClients();

public slots:
    void clientConnected();

private Q_SLOTS:
    void clearCache();
    void updateFileWatcher();

private:
    HttpServerPrivate *d;
};

} // namespace deCONZ

#endif // ZM_HTTP_SERVER_H
