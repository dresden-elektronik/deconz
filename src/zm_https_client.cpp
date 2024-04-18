#include <QCryptographicHash>
#include <QTimer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostInfo>
#include <QUrl>
#include <QVariant>
#include <QStringList>
#include "deconz/types.h"
#include "deconz/dbg_trace.h"
#include "deconz/aps_controller.h"
#include "deconz/node.h"
#include "deconz/http_client_handler.h"
#include "deconz/util.h"
#include "zm_controller.h"
#include "zm_https_client.h"
#include "zm_http_server.h"

zmHttpsClient::zmHttpsClient(QObject *parent) :
    QSslSocket(parent)
{
    m_clientState = ClientIdle;

    connect(this, SIGNAL(readyRead()),
            this, SLOT(incomingData()));

    connect(this, SIGNAL(connected()),
            this, SLOT(connectedReady()));

    connect(this, SIGNAL(disconnected()),
            this, SLOT(detachHandlers()));

    deCONZ::HttpServer *server = qobject_cast<deCONZ::HttpServer*>(parent);

    DBG_Assert(server != 0);
    if (server)
    {
        m_serverRoot = server->serverRoot();
        DBG_Assert(!m_serverRoot.isEmpty());

        if (m_serverRoot.isEmpty())
        {
            m_serverRoot = "/";
        }
    }
}

int zmHttpsClient::registerClientHandler(deCONZ::HttpClientHandler *handler)
{
    if (!handler)
    {
        return -1;
    }

    std::list<deCONZ::HttpClientHandler*>::iterator it;

    it =std::find(m_handlers.begin(), m_handlers.end(), handler);

    // already registered?
    if (it != m_handlers.end())
    {
        return -1;
    }

    // not found
    m_handlers.push_back(handler);
    return 0;
}

void zmHttpsClient::incomingData()
{
    handleHttpRequest();
}

void zmHttpsClient::connectedReady()
{
}

/*!
    Informs all handlers that the socket is no longer valid.
*/
void zmHttpsClient::detachHandlers()
{
    std::list<deCONZ::HttpClientHandler*>::iterator i = m_handlers.begin();
    std::list<deCONZ::HttpClientHandler*>::iterator end = m_handlers.end();

    for (; i != end; ++i)
    {
        (*i)->clientGone(this);
    }

    m_handlers.clear();
}


enum UrlParseState
{
    ParseMethod,
    ParseRessource,
    ParseHeader
};

void zmHttpsClient::handleHttpRequest()
{
    int pos = 0;
    char buf[4] = { 0 };

    if (m_clientState == ClientIdle)
    {
        m_clientState = ClientRecvHeader;
    }

    while ((m_clientState == ClientRecvHeader) && (bytesAvailable() > 0))
    {
        char c;
        read(&c, 1);

        m_headerBuf.append(c);

        if (c == '\r' || c == '\n')
        {
            buf[pos++] = c;

            if (pos == 4)
            {
                if (memcmp(buf, "\r\n\r\n", 4) == 0)
                {
                    QHttpRequestHeader hdr(m_headerBuf);
                    m_hdr = hdr;
                    m_clientState = ClientRecvContent;
                    // end of header detected
//                    DBG_Printf(DBG_INFO, "HTTP client hdr detected:\n%s\n", qPrintable(m_headerBuf));
                    m_headerBuf.clear();
                    break;
                }
                pos = 0;
            }
        }
        else
        {
            pos = 0;
        }
    }

    if (m_clientState != ClientRecvContent)
    {
        return;
    }

    if (m_hdr.contentLength() > 0)
    {
        uint length = m_hdr.contentLength();

        if (length > bytesAvailable())
        {
//            DBG_Printf(DBG_INFO, "Content not completely loaded (got %u of %d), abort\n", length, bytesAvailable());
            return;
        }
    }

    m_clientState = ClientIdle;

    // check if a handler is available
    std::list<deCONZ::HttpClientHandler*>::iterator i = m_handlers.begin();
    std::list<deCONZ::HttpClientHandler*>::iterator end = m_handlers.end();

    for (; i != end; ++i)
    {
        if ((*i)->isHttpTarget(m_hdr))
        {
            int ret = (*i)->handleHttpRequest(m_hdr, this);

            if (ret != 0)
            {
                DBG_Printf(DBG_INFO, "%s handle http request failed, status: %d\n", Q_FUNC_INFO, ret);
            }

            return;
        }
    }

    // not handled yet, assume file request
    handleHttpFileRequest(m_hdr);
}

int zmHttpsClient::handleHttpFileRequest(QHttpRequestHeader &hdr)
{
    QString path = hdr.path();

    const char *contentType = HttpContentHtml;

    if (path == "/")
    {
        path = "/index.html";
    }

    if (path.startsWith("/"))
    {
        if (!m_serverRoot.isEmpty())
        {
            path.prepend(m_serverRoot);
        }
        else
        {
            path.remove(0, 1); // make relative path
        }
    }


    DBG_Printf(DBG_HTTP, "HTTP client GET %s\n", qPrintable(path));

    QFile f(path);

    if (path.endsWith("css"))
    {
        contentType = HttpContentCss;
    }
    else if (path.endsWith("js"))
    {
        contentType = HttpContentJS;
    }
    else if (path.endsWith("png"))
    {
        contentType = HttpContentPNG;
    }
    else if (path.endsWith("jpg"))
    {
        contentType = HttpContentJPG;
    }
    else if (path.endsWith("svg"))
    {
        contentType = HttpContentSVG;
    }
    else if (path.endsWith("xml"))
    {
        contentType = HttpContentXML;
    }

    if (f.open(QFile::ReadOnly))
    {
        QTextStream stream(this);
        stream << "HTTP/1.1 200 OK\r\n";
        stream << "Content-Type: " << contentType << "\r\n";
        stream << "Content-Length:" << QString::number(f.size()) << "\r\n";
        stream << "\r\n";
        stream.flush();

        flush();

        while (!f.atEnd())
        {
            char c;
            f.getChar(&c);
            write(&c, 1);
        }
    }
    else
    {
        QTextStream stream(this);
        QString str = QString("<html><head></head><body>"
                    "<h1>This is not the page you are looking for</h1>"
                    "<p>The file %1 couldn't be found.</p>"
                    "</body></html>").arg(hdr.path());

        stream << "HTTP/1.1 404 Not Found\r\n";
        stream << "Content-Type: text/html\r\n";
        stream << "Content-Length:" << QString::number(str.size()) << "\r\n";
        stream << "\r\n";
        stream << str;

        DBG_Printf(DBG_INFO, "\t--> HTTP/1.1 404 Not Found\n");

    }

    flush();

    return 0;
}
