/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_NEIGHBOR_H
#define ZM_NEIGHBOR_H

#include "deconz/types.h"
#include "deconz/aps.h"
#include "deconz/timeref.h"

/*!
    \class zmNeighbor

    A neighbor entry in the zmNode neighbor's table.
 */
class /*DECONZ_DLLSPEC*/ zmNeighbor
{
public:
    zmNeighbor() = default;
    zmNeighbor(const char *buf, size_t size);
    /*! Address of the neighbor. */
    const deCONZ::Address &address() const { return m_addr; }
    deCONZ::Address &address() { return m_addr; }
    deCONZ::DeviceType deviceType() const { return deCONZ::DeviceType(m_devType); }
    deCONZ::DeviceRelationship relationship() const { return deCONZ::DeviceRelationship(m_relationship); }
    uint64_t extPanId() const { return m_extPanId; }
    void setLastSeen(deCONZ::SteadyTimeRef time) { m_lastSeen = time; }
    deCONZ::SteadyTimeRef lastSeen() const { return m_lastSeen; }
    uint8_t lqi() const { return m_lqi; }
    uint8_t rxOnWhenIdle() const { return m_rxOnWhenIdle; }
    uint8_t depth() const { return m_depth; }
    bool operator==(const zmNeighbor &rhs)
    {
        if (rhs.address().hasExt() && address().hasExt())
        {
            if (rhs.address().ext() == address().ext())
            {
                return true;
            }
        }

        return false;
    }

    deCONZ::Address m_addr{};
    uint64_t m_extPanId = 0;
    deCONZ::SteadyTimeRef m_lastSeen;
    uint8_t m_devType = deCONZ::UnknownDevice;
    uint8_t m_rxOnWhenIdle = 0x02;
    uint8_t m_relationship = deCONZ::UnknownRelation;
    uint8_t m_permitJoin = deCONZ::NeighborJoinUnknown;
    uint8_t m_depth = 0;
    uint8_t m_lqi = 0;
};

Q_DECLARE_METATYPE(zmNeighbor)

#endif // ZM_NEIGHBOR_H
