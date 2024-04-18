#ifndef ZM_HTTPS_CLIENT_H
#define ZM_HTTPS_CLIENT_H

#include <QSslSocket>
#if QT_VERSION < 0x050000
  #include <QHttpRequestHeader>
#else
  #include "deconz/qhttprequest_compat.h"
#endif
#include "deconz/types.h"

namespace deCONZ {
    class Node;
    class HttpClientHandler;
}

class zmHttpsClient : public QSslSocket
{
    Q_OBJECT
public:
    enum Constants
    {
        BufferSize = 2048
    };

    explicit zmHttpsClient(QObject *parent = 0);
    int registerClientHandler(deCONZ::HttpClientHandler *handler);

public slots:
    void incomingData();
    void connectedReady();
    void detachHandlers();
    void handleHttpRequest();
    int handleHttpFileRequest(QHttpRequestHeader &hdr);

private:
    enum ClientState
    {
        ClientIdle,
        ClientRecvHeader,
        ClientRecvContent
    };

    QString m_serverRoot;
    ClientState m_clientState;
    bool m_needSendResponse;
    quint64 m_bufferSize;
    quint8 m_buffer[BufferSize];
    quint8 m_unmask[BufferSize];
    quint8 m_sendBuffer[BufferSize];
    QHttpRequestHeader m_hdr;
    QString m_headerBuf;
    std::list<deCONZ::HttpClientHandler*> m_handlers;
};

#endif // ZM_HTTPS_CLIENT_H
