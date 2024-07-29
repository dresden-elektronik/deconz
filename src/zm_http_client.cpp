/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QCryptographicHash>
#include <QTimer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostInfo>
#include <QUrl>
#include <QVariant>
#include <QStringList>
#include "deconz/dbg_trace.h"
#include "deconz/http_client_handler.h"
#include "deconz/util.h"
#include "zm_http_client.h"
#include "zm_http_server.h"
#include "deconz/u_sstream.h"
#include "deconz/timeref.h"

#define MAX_HTTP_HEADER_LENGTH 8192

const char *HttpStatusOk           = "200 OK"; // OK
const char *HttpStatusAccepted     = "202 Accepted"; // Accepted but not complete
const char *HttpStatusBadRequest   = "400 Bad Request"; // Malformed request
const char *HttpStatusUnauthorized = "401 Unauthorized"; // Unauthorized
const char *HttpStatusForbidden    = "403 Forbidden"; // Understand request but no permission
const char *HttpStatusNotFound     = "404 Not Found"; // Requested uri not found
const char *HttpContentHtml        = "text/html; charset=utf-8";
const char *HttpContentCss         = "text/css";
const char *HttpContentJson        = "application/json; charset=utf-8";
const char *HttpContentManifestJson = "application/manifest+json";
const char *HttpContentJS          = "text/javascript";
const char *HttpContentPNG         = "image/png";
const char *HttpContentJPG         = "image/jpg";
const char *HttpContentSVG         = "image/svg+xml";
const char *HttpContentXML         = "text/xml";
const char *HttpContentAppCache    = "text/cache-manifest";
const char *HttpContentOctedStream = "application/octet-stream";
const char *HttpContentFontTtf     = "application/x-font-ttf";
const char *HttpContentFontWoff    = "application/font-woff";
const char *HttpContentFontWoff2   = "application/font-woff2";
const char *HttpContentRSS         = "application/rss+xml";

zmHttpClient::zmHttpClient(std::vector<CacheItem> &cache, QObject *parent) :
    QTcpSocket(parent),
    m_cache(cache)
{
    m_clientState = ClientIdle;
    m_headerBuf.reserve(MAX_HTTP_HEADER_LENGTH);

    connect(this, SIGNAL(readyRead()),
            this, SLOT(handleHttpRequest()));

    connect(this, SIGNAL(disconnected()),
            this, SLOT(detachHandlers()));

    deCONZ::HttpServer *server = qobject_cast<deCONZ::HttpServer*>(parent);

    DBG_Assert(server != nullptr);
    if (server)
    {
        m_serverRoot = server->serverRoot();
        DBG_Assert(!m_serverRoot.isEmpty());

        if (m_serverRoot.isEmpty())
        {
            m_serverRoot = QLatin1String("/");
        }
    }
}

zmHttpClient::~zmHttpClient()
{

}

int zmHttpClient::registerClientHandler(deCONZ::HttpClientHandler *handler)
{
    DBG_Assert(handler);
    if (!handler)
    {
        return -1;
    }

    size_t pos = m_handlers.size(); // invalid
    for (size_t i = 0; i < m_handlers.size(); i++)
    {
        if (m_handlers[i] == nullptr)
        {
            pos = i;
            break;
        }
    }

    if (pos == m_handlers.size()) // no free slot
    {
        return -1;
    }

    const auto it = std::find(m_handlers.cbegin(), m_handlers.cend(), handler);

    // already registered?
    if (it != m_handlers.end())
    {
        return -1;
    }

    QObject *obj = dynamic_cast<QObject*>(handler);
    Q_ASSERT(obj);
    connect(obj, SIGNAL(destroyed()), this, SLOT(handlerDeleted()));

    m_handlers[pos] = handler;
    return 0;
}

/*!
    Informs all handlers that the socket is no longer valid.
*/
void zmHttpClient::detachHandlers()
{
    for (auto &handler : m_handlers)
    {
        if (handler)
        {
            handler->clientGone(this);
            handler = nullptr;
        }
    }
}


enum UrlParseState
{
    ParseMethod,
    ParseRessource,
    ParseHeader
};

void zmHttpClient::handleHttpRequest()
{
    if (m_clientState == ClientIdle)
    {
        m_clientState = ClientRecvHeader;
    }

    int hdrEnd = 0;

    while (m_clientState == ClientRecvHeader && bytesAvailable() > 0)
    {
        char c;
        read(&c, 1);
        hdrEnd <<= 8;
        hdrEnd |= c;

        if (m_headerBuf.size() >= MAX_HTTP_HEADER_LENGTH)
        {
            m_headerBuf.clear();
            m_clientState = ClientIdle;
            QTextStream stream(this);
            stream << "HTTP/1.1 431 Request Header Fields Too Large\r\n";
            stream << "Content-Length: 0\r\n";
            stream << "\r\n";
            flush();
            close();
            return;
        }

        m_headerBuf.push_back(c);

        if ((hdrEnd & 0xffffffffL) == 0x0d0a0d0aL) // \r\n\r\n
        {
            // end of header detected
            m_headerBuf.push_back('\0');
            if (!m_hdr.update(&m_headerBuf.front(), m_headerBuf.size()))
            {
                m_clientState = ClientIdle;
                QTextStream stream(this);
                switch (m_hdr.parseStatus())
                {
                case Http::HttpStatusOk: break;
                case Http::HttpStatusBadRequest: stream << "HTTP/1.1 400 Bad Request\r\n"; break;
                case Http::HttpStatusMethodNotAllowed: stream << "HTTP/1.1 405 Method Not Allowed\r\n"; break;
                case Http::HttpStatusPayloadTooLarge: stream << "HTTP/1.1 413 Payload Too Large\r\n"; break;
                case Http::HttpStatusUriTooLong: stream << "HTTP/1.1 414 URI Too Long\r\n"; break;
                case Http::HttpStatusRequestHeaderFieldsTooLarge: stream << "HTTP/1.1 431 Request Header Fields Too Large\r\n"; break;
                }

                stream << "\r\n";
                flush();
                close();
                return;
            }
            m_clientState = ClientRecvContent;
            m_headerBuf.clear();
            break;
        }
    }

    if (m_clientState != ClientRecvContent)
    {
        return;
    }

    if (m_hdr.contentLength() > 0)
    {
        const uint length = static_cast<uint>(m_hdr.contentLength());

        if (length > bytesAvailable())
        {
//            //DBG_Printf(DBG_HTTP, "Content not completely loaded (got %d of %u), wait 20ms\n", bytesAvailable(), length);
//            if (!waitForReadyRead(20) || length > bytesAvailable())
            {
                //DBG_Printf(DBG_HTTP, "Content not completely loaded (got %d of %u), fetch rest [2]\n", bytesAvailable(), length);
                return;
            }
        }
    }

    m_clientState = ClientIdle;

    // check if a handler is available
    for (auto *handler : m_handlers)
    {
        if (handler && handler->isHttpTarget(m_hdr))
        {
            const int ret = handler->handleHttpRequest(m_hdr, this);

            if (ret != 0)
            {
                DBG_Printf(DBG_HTTP, "HTTP client handle request failed, status: %d\n", ret);
            }
            flush();
            return;
        }
    }

    handleHttpFileRequest(m_hdr);
    close();
}

static long cacheSessionHash = 0;

/*
 * This function adds deCONZ session query strings to force browsers to refresh imported
 * modules once per deCONZ session.
 *
 * import { foo } from './bar.js';
 * ... becomes ...
 * import { foo } from './bar.js?12345';
 */
static void preProcessJavascriptModulesForCache(zmHttpClient::CacheItem &item)
{
    QByteArray tmp;
    const int sz = item.content.size();
    const QByteArray &content = item.content;

    char versionString[32];

    {
        U_SStream ss;
        U_sstream_init(&ss, versionString, sizeof(versionString));
        U_sstream_put_str(&ss, "?");
        U_sstream_put_long(&ss, cacheSessionHash);
    }

    tmp.reserve(sz);

    enum ParseState
    {
        PS_Initial,
        PS_FromF,
        PS_FromR,
        PS_FromO,
        PS_FromM,
        PS_Quote0

    } state = PS_Initial;

    char quoteChar = 0;
    int pos_quote0 = 0;

    for (int pos = 0; pos < sz; pos++)
    {
        char ch = content.at(pos);

        if      (state == PS_Initial) { if (ch == 'f') state = PS_FromF;}
        else if (state == PS_FromF) state = (ch == 'r') ? PS_FromR : PS_Initial;
        else if (state == PS_FromR) state = (ch == 'o') ? PS_FromO : PS_Initial;
        else if (state == PS_FromO) state = (ch == 'm') ? PS_FromM : PS_Initial;
        else if (state == PS_FromM)
        {
            if      (ch == ' ' || ch == '\t') { }
            else if (ch == '\'' || ch == '"') { state = PS_Quote0; pos_quote0 = pos; quoteChar = ch; }
            else                              { state = PS_Initial; }
        }
        else if (state == PS_Quote0)
        {
            if (ch == quoteChar && (pos - pos_quote0) > 4)
            {
                // check .js extension
                if (content.at(pos - 3) == '.' && content.at(pos - 2) == 'j' && content.at(pos - 1) == 's')
                {
                    tmp.append(versionString);

#ifdef DECONZ_DEBUG_BUILD
                    QByteArray a = content.mid(pos_quote0, (pos - pos_quote0) + 1);
                    DBG_Printf(DBG_INFO, "cache alter: %s\n", qPrintable(a));
#endif
                }

                state = PS_Initial;
            }
        }
        else
        {
            state = PS_Initial;
        }

        tmp.append(ch);
    }

    // cache info was added?
    if (tmp.size() > content.size())
    {
        item.content = tmp;
    }
}

/*
 * <script defer src="js/poll.js?12345"></script>
 *
 * This function replaces the ?12345 part in script tags with a deCONZ session
 * string to force browsers to refresh cache once per deCONZ session.
 * This is needed since the Javascript files may themself import ES6 modules,
 * and the Javascript modules will also be loaded with the deCONZ session query string.
 */
static void preProcessHtmlForCache(zmHttpClient::CacheItem &item)
{
    QByteArray tmp;
    const int sz = item.content.size();
    const QByteArray &content = item.content;

    char versionString[32];

    {
        U_SStream ss;
        U_sstream_init(&ss, versionString, sizeof(versionString));
        U_sstream_put_long(&ss, cacheSessionHash);
    }

    tmp.reserve(sz);

    enum ParseState
    {
        PS_Initial,
        PS_Bracket,
        PS_ScriptS,
        PS_ScriptC,
        PS_Equal,
        PS_Quote0

    } state = PS_Initial;

    int pos_quote0 = 0;
    int pos_questionmark = 0;

    for (int pos = 0; pos < sz; pos++)
    {
        char ch = content.at(pos);

        if      (state == PS_Initial) { if (ch == '<') state = PS_Bracket; }
        else if (state == PS_Bracket)
        {
            if (ch == 's') state = PS_ScriptS;
            else           state = PS_Initial;
        }
        else if (state == PS_ScriptS) // note: just look for <sc is enough to filter for <script
        {
            if (ch == 'c') state = PS_ScriptC;
            else           state = PS_Initial;
        }
        else if (ch == '>') { state = PS_Initial; }
        else if (state == PS_ScriptC) { if (ch == '=') state = PS_Equal; }
        else if (state == PS_Equal)
        {
            if (ch == '"')
            {
                state = PS_Quote0;
                pos_quote0 = pos;
                pos_questionmark = 0;
            }
            else
            {
                state = PS_Initial;
            }
        }
        else if (state == PS_Quote0)
        {
            if (ch == '?')
            {
                // "poll.js?..."  look for .js
                if (content.at(pos - 3) == '.' && content.at(pos - 2) == 'j' && content.at(pos - 1) == 's')
                {
                    pos_questionmark = pos;
                }
                else
                {
                    state = PS_Initial;
                }
            }
            else if (ch == '"')
            {
                state = PS_Initial;

                int len = pos - pos_quote0;
                if (len > 4 && pos_questionmark != 0)
                {
                    tmp.append(versionString);
#ifdef DECONZ_DEBUG_BUILD
                    QByteArray a = content.mid(pos_quote0, len + 1);
                    DBG_Printf(DBG_INFO, "html alter: %s (%s)\n", qPrintable(a), versionString);
#endif
                }
            }
        }
        else
        {
            state = PS_Initial;
        }

        tmp.append(ch);
    }

    // cache info was added?
    if (tmp.size() > content.size())
    {
        item.content = tmp;
    }
}

static const zmHttpClient::CacheItem *getCacheItem(const QString &path, std::vector<zmHttpClient::CacheItem> &cache)
{
#ifdef DECONZ_DEBUG_BUILD
//    cache.clear();
#endif

    if (cacheSessionHash == 0)
    {
        cacheSessionHash = (long)(deCONZ::systemTimeRef().ref & 0xFFFFF);
    }

    auto i = std::find_if(cache.cbegin(), cache.cend(), [&path](const auto &item)
    {
        return item.path == path;
    });

    if (i != cache.cend())
    {
        return &*i;
    }

    QFile f(path);

    if (!f.exists() || !f.open(QFile::ReadOnly))
    {
        return nullptr;
    }

    zmHttpClient::CacheItem item;

    item.content = f.readAll();

    if (item.content.isEmpty())
    {
        return nullptr;
    }

    // modify sources to have a per deCONZ session URL query abc.js?12345
    if (path.endsWith(QLatin1String(".js")))
    {
        preProcessJavascriptModulesForCache(item);
    }
    else if (path.endsWith(QLatin1String(".html")))
    {
        preProcessHtmlForCache(item);
    }

    item.fileSize = item.content.size();
    item.content = qCompress(item.content, -1);

    {
        QCryptographicHash hash(QCryptographicHash::Md5);
        hash.addData(item.content);
        item.etag = hash.result().toBase64();
    }

    {
        QFileInfo fi(f);
        const QDateTime lastModified = fi.lastModified();
        if (lastModified.isValid())
        {
            static const char *dayOfWeek[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
            static const char *month[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

            QString lm = lastModified.toString(QLatin1String("%1, dd %2 yyyy HH:mm:ss GMT"));

            item.lastModified = lm.arg(dayOfWeek[lastModified.date().dayOfWeek() - 1],
                                       month[lastModified.date().month() - 1]);

        }
    }

    item.path = path;

    cache.push_back(std::move(item));

#ifdef DECONZ_DEBUG_BUILD
    unsigned long cacheSize = 0;
    for (size_t i = 0; i < cache.size(); i++)
        cacheSize += cache[i].content.size();

    DBG_Printf(DBG_INFO, "HTTP cache size: %lu kB\n", cacheSize / 1024);
#endif

    return &cache.back();
}

int zmHttpClient::handleHttpFileRequest(const QHttpRequestHeader &hdr)
{
    QUrl url (hdr.path());
    QString path = url.path();

    const char *contentType = nullptr; // supported or 404
    bool isPwa = false;

    if (path.size() > 1 && path.endsWith('/'))
    {
        path.chop(1);
    }

    if (path == QLatin1String("/pwa"))
    {
        isPwa = true;
    }

    if (path == QLatin1String("/") || isPwa)
    {
        if (QFile::exists(m_serverRoot + QLatin1String("/pwa/index.html")))
        {
            QTextStream stream(this);
            const QString str = QString("<html><head>"
                                  "<title>Moved</title>"
                                  "</head><body>"
                                  "<h1>Moved</h1>"
                                  "<p>moved to /pwa/index.html</p>"
                                  "</body></html>");

            stream << "HTTP/1.1 301 Moved Permanently\r\n"
                      "Content-Type: text/html\r\n"
                      "Location: /pwa/index.html\r\n";
            stream << "Content-Length:" << QString::number(str.size()) << "\r\n"
                                                                          "\r\n";
            stream << str;
            flush();
            return 0;
        }

        path = QLatin1String("/index.html");
    }

    if (path.startsWith(QLatin1String("/")))
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

//    DBG_Printf(DBG_HTTP, "HTTP client GET %s\n", qPrintable(path));

    QFile f(path);

    int maxAge = 60 * 60 * 2; // 2 hours

    if (path.endsWith(QLatin1String("css")))
    {
        contentType = HttpContentCss;
    }
    else if (path.endsWith(QLatin1String("js")))
    {
        contentType = HttpContentJS;
    }
    else if (path.endsWith(QLatin1String("json")))
    {
        contentType = HttpContentJson;
    }
    else if (path.endsWith(QLatin1String("png")))
    {
        contentType = HttpContentPNG;
    }
    else if (path.endsWith(QLatin1String("jpg")))
    {
        contentType = HttpContentJPG;
    }
    else if (path.endsWith(QLatin1String("svg")))
    {
        contentType = HttpContentSVG;
    }
    else if (path.endsWith(QLatin1String("html")))
    {
        contentType = HttpContentHtml;
        maxAge = 60 * 5;
    }
    else if (path.endsWith(QLatin1String("xml")))
    {
        contentType = HttpContentXML;
    }
    else if (path.endsWith(QLatin1String("rss")))
    {
        contentType = HttpContentRSS;
    }
    else if (path.endsWith(QLatin1String("ttf")))
    {
        contentType = HttpContentFontTtf;
    }
    else if (path.endsWith(QLatin1String("woff")))
    {
        contentType = HttpContentFontWoff;
    }
    else if (path.endsWith(QLatin1String("woff2")))
    {
        contentType = HttpContentFontWoff2;
    }
    else if (path.endsWith(QLatin1String("appcache")))
    {
        contentType = nullptr; // don't use this anymre
    }
    else if (path.endsWith(QLatin1String("manifest.json")))
    {
        contentType = HttpContentManifestJson;
    }
    else if (path.endsWith(QLatin1String("deCONZ.tar.gz")))
    {
        contentType = HttpContentOctedStream;
        path = deCONZ::getStorageLocation(deCONZ::ApplicationsDataLocation);
        path += "/deCONZ.tar.gz";
    }

    const CacheItem *cacheItem = getCacheItem(path, m_cache);

    if (contentType && cacheItem)
    {
        QTextStream stream(this);

        const auto data = qUncompress(cacheItem->content);

        static const QLatin1String keepAliveHeader("Keep-Alive: timeout=6\r\n");

        if (contentType == HttpContentOctedStream &&
            path.endsWith(QLatin1String(".tar.gz")))
        {
            QString now = QDate::currentDate().toString("yyyy-MM-dd");
            stream << "HTTP/1.1 200 OK\r\n";
            stream << "Pragma: public\r\n";
            stream << "Expires: 0\r\n";
            stream << "Cache-Control: must-revalidate, post-check=0, pre-check=0\r\n";
            stream << "Cache-Control: public\r\n";
            stream << "Content-Description: File Transfer\r\n";
            stream << "Content-Type: application/octet-stream\r\n";
            stream << "Content-Disposition: attachment; filename=\"raspbee_gateway_config_";
            stream << now << ".dat\"\r\n";
            stream << "Content-Transfer-Encoding: binary\r\n";
            stream << "Content-Length:" << QString::number(data.size()) << "\r\n";
            stream << "\r\n";
            stream.flush();

            flush();

            write(data);

            flush();

            return 0;
        }

        if (hdr.hasKey(QLatin1String("If-None-Match")))
        {
            QString ifNoneMatch = hdr.value(QLatin1String("If-None-Match"));

            if (ifNoneMatch == cacheItem->etag)
            {
                stream << "HTTP/1.1 304 Not Modified\r\n";
                stream << "ETag: " << cacheItem->etag << "\r\n";
                stream << keepAliveHeader;
                if (contentType == HttpContentAppCache)
                {
                    stream << "Cache-Control: no-cache\r\n"; // use ETag for cache optimization
                }
                else
                {
                    stream << "Cache-Control: max-age=" << maxAge << "\r\n";
                }
                stream << "\r\n";
                flush();
                return 0;
            }
        }

        stream << "HTTP/1.1 200 OK\r\n";
        stream << "ETag: " << cacheItem->etag << "\r\n";
        stream << "Content-Type: " << contentType << "\r\n";
        stream << "Content-Length:" << QString::number(data.size()) << "\r\n";
        if (contentType == HttpContentAppCache)
        {
            stream << "Cache-Control: no-cache\r\n"; // use ETag for cache optimization
        }
        else
        {
            stream << "Cache-Control: max-age=" << maxAge << "\r\n";
        }
//        if (cacheItem->gzip)
//        {
//            stream << "Content-Encoding: gzip\r\n";
//        }
        stream << keepAliveHeader;
        stream << "Last-Modified:" << cacheItem->lastModified << "\r\n";

        stream << "\r\n";
        stream.flush();

        flush();

        write(data);
#ifdef Q_OS_WIN
        m_cache.clear();
#endif
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

        DBG_Printf(DBG_HTTP, "\t%s --> HTTP/1.1 404 Not Found\n", qPrintable(hdr.path()));

    }

    if (hdr.method() == QLatin1String("HEAD"))
    {
        close();
        deleteLater();
    }
    else
    {
#ifndef QT_DEBUG // in debug mode SIGPIPE will be thrown too often
        flush();
#endif
    }

    return 0;
}

void zmHttpClient::handlerDeleted()
{
    for (auto &handler : m_handlers)
    {
        handler = nullptr;
    }
}
