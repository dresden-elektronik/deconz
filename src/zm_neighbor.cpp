/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include "zm_neighbor.h"
#include "deconz/buffer_helper.h"

zmNeighbor::zmNeighbor(const char *buf, size_t size)
{
    if (size != 22)
    {
        return;
    }

    const uint8_t *p = reinterpret_cast<const uint8_t*>(buf);

    p = get_u64_le(p, &m_extPanId);

    {
        uint64_t u64;
        p = get_u64_le(p, &u64);
        m_addr.setExt(u64);
    }

    {
        uint16_t u16;
        p = get_u16_le(p, &u16);
        m_addr.setNwk(u16);
    }

    if      ((*p & 0x03) == 0x03)  m_devType = deCONZ::UnknownDevice;
    else if (*p & 0x01)            m_devType = deCONZ::Router;
    else if (*p & 0x02)            m_devType = deCONZ::EndDevice;
    else      /* 0x00 */           m_devType = deCONZ::Coordinator;

    m_rxOnWhenIdle = static_cast<quint8>((*p & 0x0C) >> 2);

    quint8 rel = static_cast<quint8>((*p & 0x70) >> 4);
    if      (rel == 0)         m_relationship = deCONZ::ParentRelation;
    else if (rel == 1)         m_relationship = deCONZ::ChildRelation;
    else if (rel == 2)         m_relationship = deCONZ::SiblingRelation;
    else if (rel == 3)         m_relationship = deCONZ::UnknownRelation;
    else if (rel == 4)         m_relationship = deCONZ::PreviousChildRelation;

    p++;
    if (*p & 0x01)  m_permitJoin = deCONZ::NeighborAcceptJoin;
    if (*p & 0x02)  m_permitJoin = deCONZ::NeighborJoinUnknown;
    else /* 0x00 */ m_permitJoin = deCONZ::NeighborNotAcceptJoin;

    p++;
    m_depth = *p++;
    m_lqi = *p++;
}
