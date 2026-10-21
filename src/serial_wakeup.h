#ifndef DECONZ_SERIAL_WAKEUP_H
#define DECONZ_SERIAL_WAKEUP_H

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

// A level-triggered notification for poll(). The shared state is authoritative;
// pipe bytes only mean that the worker must check that state again.
class SerialWakeup
{
public:
    SerialWakeup() = default;
    SerialWakeup(const SerialWakeup &) = delete;
    SerialWakeup &operator=(const SerialWakeup &) = delete;

    ~SerialWakeup()
    {
        for (int fd : fds)
            if (fd >= 0) ::close(fd);
    }

    bool init()
    {
        if (::pipe(fds) != 0)
            return false;
        for (int fd : fds)
        {
            const int flags = ::fcntl(fd, F_GETFL);
            const int fdflags = ::fcntl(fd, F_GETFD);
            if (flags < 0 || fdflags < 0 ||
                ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
                ::fcntl(fd, F_SETFD, fdflags | FD_CLOEXEC) < 0)
                return false;
        }
        return true;
    }

    int descriptor() const { return fds[0]; }

    void notify() const
    {
        const char byte = 1;
        ssize_t result;
        do { result = ::write(fds[1], &byte, 1); }
        while (result < 0 && errno == EINTR);
        // EAGAIN means a notification is already pending; do not block the
        // main thread. Descriptors remain alive until the worker is joined.
    }

    void drain() const
    {
        char bytes[128];
        ssize_t result;
        do { result = ::read(fds[0], bytes, sizeof(bytes)); }
        while (result > 0 || (result < 0 && errno == EINTR));
    }

private:
    int fds[2] = {-1, -1};
};
#endif
