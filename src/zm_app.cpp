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
#include <actor/service.h>
#include <actor/plugin_loader.h>

#include <deconz/atom_table.h>
#include <deconz/timeref.h>
#include <deconz/u_timer.h>
#include <deconz/u_memory.h>
#include "zm_app.h"
#include "zm_http_server.h"

bool gHeadlessVersion = false;

#define MAX_MAIN_MQ_MESSAGAES 512

struct main_mq
{
    int running;
    struct am_message msg_buf[MAX_MAIN_MQ_MESSAGAES];
    unsigned char msg_data[1 << 18];
    unsigned char out_data[AM_MAX_MESSAGE_SIZE];

    struct am_message_queue queue;
};

static U_Thread mqThread;
static int64_t tref; // track timer differences
static struct main_mq main_mq;

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
    for (;main_mq.running;)
    {
        AM_WaitMessageQueue(&main_mq.queue);

        // notify main thread to process message queue
        emit _instance->amMessageReceived();
    }

    U_thread_exit(0);
}

class AppPrivate
{
public:
    deCONZ::HttpServer *httpServer;
};

zmApp::zmApp(int &argc, char **argv) :
    QApplication(argc, argv),
    d_ptr(new AppPrivate)
{
    _instance = this;

    AT_Init(1 << 15);

    struct am_message_queue *mq;

    AM_Init();

    mq = &main_mq.queue;
    mq->id = 0;
    mq->msg_buf_size = sizeof(main_mq.msg_buf) / sizeof(main_mq.msg_buf[0]);
    mq->msg_data_size = sizeof(main_mq.msg_data);
    mq->out_data_size = sizeof(main_mq.out_data);
    mq->msg_buf = &main_mq.msg_buf[0];
    mq->msg_data = &main_mq.msg_data[0];
    mq->out_data = &main_mq.out_data[0];
    AM_RegisterMessageQueue(mq);
    main_mq.running = 1;

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

    d_ptr->httpServer = new deCONZ::HttpServer(this);
}

zmApp::~zmApp()
{
    main_mq.running = 0;
    AM_UnloadPlugins();
    AM_Shutdown();
    AM_Destroy();

    AT_Destroy();

    delete d_ptr;
    d_ptr = nullptr;
}

/* This called by seperate thread via queued connection on signal amMessageReceived(). */
void zmApp::actorTick()
{
    AM_Tick(&main_mq.queue);
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

    d_ptr->httpServer->processClients();
}
