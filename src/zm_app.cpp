/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <QAbstractEventDispatcher>
#include <QTimer>
#include <actor/service.h>
#include <actor/plugin_loader.h>

#include <deconz/atom_table.h>
#include <deconz/timeref.h>
#include <deconz/u_assert.h>
#include <deconz/u_timer.h>
#include <deconz/u_threads.h>
#include <deconz/u_memory.h>
#include "zm_app.h"
#include "zm_http_server.h"

bool gHeadlessVersion = false;

#define MAX_MAIN_MQ_MESSAGAES 512

static U_Thread mqThread;
static int64_t tref; // track timer differences
std::atomic_bool mqRunning;
struct am_message_queue *main_mq = nullptr;

static zmApp *_instance;

void *AM_Alloc(unsigned long size)
{
    return U_Alloc(size);
}

void AM_Free(void *ptr)
{
    if (ptr)
    {
        U_Free(ptr);
    }
}

/* Wait for main queue messages in own thread. */
static void mqThreadFunc(void *arg)
{
    U_thread_set_name(&mqThread, "main mq");
    mqRunning = true;

    for (;mqRunning;)
    {
        U_ASSERT(main_mq);
        AM_WaitMessageQueue(main_mq);

        // notify main thread to process message queue
        if (mqRunning)
            emit _instance->amMessageReceived();
    }

    main_mq = nullptr;
    U_thread_exit(0);
}

class AppPrivate
{
public:
    deCONZ::HttpServer *httpServer = nullptr;
};

zmApp::zmApp(int &argc, char **argv) :
    QApplication(argc, argv),
    d_ptr(new AppPrivate)
{
    _instance = this;

    AT_Init(1 << 15);

    struct am_message_queue *mq;

    AM_Init();

    main_mq = AM_CreateMessageQueue(0, MAX_MAIN_MQ_MESSAGAES);

    tref = deCONZ::steadyTimeRef().ref;

    if (eventDispatcher())
    {
        connect(eventDispatcher(), &QAbstractEventDispatcher::awake,
                this, &zmApp::eventQueueIdle);
    }

    // The signal is called from mqThreadFunc() in non gui thread.
    // Everytime a message is received from some thread, the main thread
    // wakes up and delivers the message.
    connect(this, &zmApp::amMessageReceived, this, &zmApp::actorTick, Qt::QueuedConnection);

    // extra thread to wait for messages
    U_thread_create(&mqThread, mqThreadFunc, &main_mq);

    U_TimerInit(AM_ApiFunctions());

    d_ptr->httpServer = nullptr;
    QTimer::singleShot(200, [this](){
        d_ptr->httpServer = new deCONZ::HttpServer(this);
    });
}

zmApp::~zmApp()
{
    AM_UnloadPlugins();
    AM_Shutdown();
    mqRunning = false;
    U_thread_join(&mqThread);
    U_ASSERT(main_mq == nullptr);
    AM_Destroy();

    AT_Destroy();

    delete d_ptr;
    d_ptr = nullptr;
}

/* This called by seperate thread via queued connection on signal amMessageReceived(). */
void zmApp::actorTick()
{
    if (mqRunning)
    {
        U_ASSERT(main_mq);
        if (0 == AM_Tick(main_mq)) // shutdown was called
        {
            mqRunning = false;
        }
    }
}

void zmApp::eventQueueIdle()
{
    const int64_t now = deCONZ::steadyTimeRef().ref;

    if (now < tref) // re-adjust, shouldn't happen
        tref = now;

    int64_t diff = now - tref;
    if (diff > 0)
    {
        tref = now;
        U_TimerTick(diff);
    }
}
