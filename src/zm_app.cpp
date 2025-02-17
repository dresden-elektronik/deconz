/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <stdlib.h> /* malloc, free */
#include <QAbstractEventDispatcher>
#include <actor/service.h>
#include <actor/plugin_loader.h>

#include <deconz/atom_table.h>
#include <deconz/u_memory.h>
#include "zm_app.h"
#include "zm_http_server.h"

bool gHeadlessVersion = false;

#define MAX_MAIN_MQ_MESSAGAES 512

struct main_mq
{
    struct am_message msg_buf[MAX_MAIN_MQ_MESSAGAES];
    unsigned char msg_data[1 << 18];
    unsigned char out_data[AM_MAX_MESSAGE_SIZE];

    struct am_message_queue queue;
};

static struct main_mq main_mq;

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

class AppPrivate
{
public:
    deCONZ::HttpServer *httpServer;
};

zmApp::zmApp(int &argc, char **argv) :
    QApplication(argc, argv),
    d_ptr(new AppPrivate)
{
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

    /* this should later run in its own thread without Qt */
    if (eventDispatcher())
    {
        connect(eventDispatcher(), &QAbstractEventDispatcher::awake,
                this, &zmApp::eventQueueIdle);
    }

    d_ptr->httpServer = new deCONZ::HttpServer(this);
}

zmApp::~zmApp()
{
    AM_UnloadPlugins();
    AM_Shutdown();
    AM_Destroy();

    AT_Destroy();

    delete d_ptr;
    d_ptr = nullptr;
}

void zmApp::eventQueueIdle()
{
    AM_Tick(&main_mq.queue);

    d_ptr->httpServer->processClients();
}
