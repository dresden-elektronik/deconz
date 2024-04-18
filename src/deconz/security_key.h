/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef SECURITY_KEY_H
#define SECURITY_KEY_H

#include "deconz/types.h"
#include "deconz/aps.h"

class SecKey
{
public:
    SecKey()
    {
        m_keySize = KeySize128;
    }

    enum KeySize
    {
        KeySize128 = 16,
        KeySizeMax = 16
    };

    uint8_t at(int idx) const
    {
        if (idx < m_keySize)
        {
            return m_key[idx];
        }

        return 0;
    }

    KeySize size() const { return m_keySize; }

    void setData(const uint8_t *key, KeySize size)
    {
        m_keySize = size;

        for (int i = 0; i < size; i++)
        {
            m_key[i] = key[i];
        }
    }

private:
    KeySize m_keySize;
    uint8_t m_key[KeySizeMax];
};

class SecKeyPair
{
public:
    deCONZ::Address &address() { return m_addr; }
    const deCONZ::Address &address() const { return m_addr; }
    SecKey &key() { return m_key; }
    const SecKey &key() const { return m_key; }

private:
    deCONZ::Address m_addr;
    SecKey m_key;
};

#endif // SECURITY_KEY_H
