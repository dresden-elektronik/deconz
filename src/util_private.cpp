/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include "deconz/aps.h"
#include "deconz/util_private.h"

static void (*notifyHandler)(UtilEvent event, void *data);
static deCONZ::ApsAddressMode dstAddrMode;
static uint8_t dstEndpoint;
static deCONZ::Address *dstAddr = 0;

namespace deCONZ {

bool getDestination(Address *addr, deCONZ::ApsAddressMode *addrMode, quint8 *endpoint)
{
    if (!addr || !addrMode || !endpoint)
    {
        return false;
    }

    if (!dstAddr)
    {
        dstAddr = new Address;
    }

    *addr = *dstAddr;
    *addrMode = dstAddrMode;
    *endpoint = dstEndpoint;

    return true;
}

void setDestination(const Address &addr, deCONZ::ApsAddressMode addrMode, quint8 endpoint)
{
    if (!dstAddr)
    {
        dstAddr = new Address;
    }

    if ((*dstAddr == addr) && (dstAddrMode == addrMode) && (dstEndpoint == endpoint))
    {
        return;
    }

    *dstAddr = addr;
    dstEndpoint = endpoint;
    dstAddrMode = addrMode;

    utilNotify(UE_DestinationAddressChanged, 0);
}

void utilSetNotifyHandler(void (*handler)(UtilEvent event, void *data))
{
    notifyHandler = handler;
}

void utilNotify(UtilEvent event, void *data)
{
    if (notifyHandler)
    {
        notifyHandler(event, data);
    }
}

}
