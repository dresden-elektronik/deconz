#include "serial_wakeup.h"
#include <poll.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <algorithm>
// Unlike assert(), CHECK always evaluates its expression, also with NDEBUG.
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
using Clock = std::chrono::steady_clock;
int main()
{
    SerialWakeup w; CHECK(w.init());
    CHECK(fcntl(w.descriptor(), F_GETFL) & O_NONBLOCK);
    CHECK(fcntl(w.descriptor(), F_GETFD) & FD_CLOEXEC);
    pollfd p{w.descriptor(), POLLIN, 0};
    CHECK(poll(&p, 1, 0) == 0);
    // Saturation must not block producers or lose the pending notification.
    for (int i=0; i<100000; i++) w.notify();
    CHECK(poll(&p, 1, 0) == 1); w.drain();
    CHECK(poll(&p, 1, 0) == 0);
    // Notifications before entering poll must remain readable.
    w.notify(); CHECK(poll(&p, 1, -1) == 1); w.drain();
    std::atomic<int> requested{0}, completed{0}, ready{0};
    std::atomic<bool> running{true};
    int serial[2]; CHECK(pipe(serial)==0);
    std::atomic<int> received{0};
    // Mirrors PL_Thread0: check state, poll, drain only when notified.
    std::thread worker([&] {
        pollfd fds[2]={{serial[0],POLLIN,0},{w.descriptor(),POLLIN,0}};
        while (true) {
            if (!running.load()) break;
            completed.store(requested.load());
            ready.store(1);
            int n=poll(fds,2,-1); CHECK(n>0);
            if (fds[1].revents & POLLIN) w.drain();
            if (fds[0].revents & POLLIN) { char c; CHECK(read(serial[0],&c,1)==1); received++; }
        }
    });
    while (!ready.load()) std::this_thread::yield();
    std::vector<double> latency;
    for(int i=1;i<=1000;i++) {
        auto t=Clock::now();requested.store(i);w.notify();
        while (completed.load()<i) { CHECK(Clock::now()-t<std::chrono::seconds(1)); std::this_thread::yield(); }
        latency.push_back(std::chrono::duration<double,std::micro>(Clock::now()-t).count());
    }
    // Real serial readiness wakes the same blocking poll independently of TX.
    char c='x'; CHECK(write(serial[1],&c,1)==1);
    auto t=Clock::now();
    while (!received.load()) {CHECK(Clock::now()-t<std::chrono::seconds(1));std::this_thread::yield();}
    // Exercise drain/state/poll races under four concurrent producers.
    std::vector<std::thread> producers;
    for(int i=0;i<4;i++) producers.emplace_back([&]{for(int j=0;j<10000;j++){requested.fetch_add(1);w.notify();}});
    for(auto &th:producers) th.join();
    t=Clock::now();
    while(completed.load()!=requested.load()) {CHECK(Clock::now()-t<std::chrono::seconds(1));std::this_thread::yield();}
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    t=Clock::now();running.store(false);w.notify();worker.join();
    CHECK(Clock::now()-t<std::chrono::seconds(1));
    close(serial[0]);close(serial[1]);
    std::sort(latency.begin(),latency.end());
    std::cout<<"PASS: saturation, pre-poll notification, RX readiness, 40000 concurrent notifications, idle shutdown\n"
             <<"TX wake-to-observation latency us: median="<<latency[500]<<" p95="<<latency[950]<<" max="<<latency.back()<<"\n";
}
