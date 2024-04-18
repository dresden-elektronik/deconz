/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef DECONZ_UTIL_PRIVATE_H
#define DECONZ_UTIL_PRIVATE_H


enum UtilEvent
{
    UE_DestinationAddressChanged
};

namespace deCONZ {

bool getDestination(Address *addr, ApsAddressMode *addrMode, quint8 *endpoint);
void setDestination(const Address &addr, deCONZ::ApsAddressMode addrMode, quint8 endpoint);
void utilSetNotifyHandler(void (*handler)(UtilEvent event, void *data));
void utilNotify(UtilEvent event, void *data);

} // namespace deCONZ

#endif // DECONZ_UTIL_PRIVATE_H
