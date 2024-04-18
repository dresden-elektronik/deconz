#ifndef QHTTPREQUEST_COMPAT_OLD_H
#define QHTTPREQUEST_COMPAT_OLD_H

class QString;
class QUrl;

class QHttpRequestHeaderPrivateOld;

class DECONZ_DLLSPEC QHttpRequestHeaderOld
{
public:
    QHttpRequestHeaderOld();
    /*! Copy constructor. */
    QHttpRequestHeaderOld(const QHttpRequestHeaderOld &other);
    /*! Copy assignment operator. */
    QHttpRequestHeaderOld& operator=(const QHttpRequestHeaderOld &other);
    QHttpRequestHeaderOld(const QString &str);
    QHttpRequestHeaderOld(const QString &method, const QString &path);
    ~QHttpRequestHeaderOld();
    bool hasKey(const QString &key) const;
    int contentLength() const;
    const QString &path() const;
    const QString &method() const;
    const QString &value(const QString &key) const;
    void setRequest(const QString &method, const QString &url);
    const QUrl &url() const;

private:
    QHttpRequestHeaderPrivateOld *d_ptr;
};

#endif // QHTTPREQUEST_COMPAT_OLD_H

