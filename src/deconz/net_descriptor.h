/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef NET_DESCRIPTOR_H
#define NET_DESCRIPTOR_H

#include "deconz/types.h"
#include "deconz/aps.h"
#include "deconz/security_key.h"

/*!
    \class zmNet

    A network descriptor holds all parameters that are
    relevant to a network, including security parameters.
 */
class zmNet
{
public:
    zmNet() = default;
    deCONZ::Address &pan() { return m_pan; }
    const deCONZ::Address &pan() const { return m_pan; }
    deCONZ::Address &panAps() { return m_panAps; }
    const deCONZ::Address &panAps() const { return m_panAps; }
    deCONZ::Address &coordinator() { return m_coord; }
    const deCONZ::Address &coordinator() const { return m_coord; }
    deCONZ::Address &parent() { return m_parent; }
    const deCONZ::Address &parent() const { return m_parent; }
    deCONZ::Address &ownAddress() { return m_ownAddr; }
    const deCONZ::Address &ownAddress() const { return m_ownAddr; }
    deCONZ::Address &trustCenterAddress() { return m_tcAddr; }
    const deCONZ::Address &trustCenterAddress() const { return m_tcAddr; }
    bool predefinedPanId() const { return m_predefPanid; }
    void setPredefinedPanId(bool predefined) { m_predefPanid = predefined; }
    bool staticAddress() const { return m_staticAddr; }
    void setStaticAddress(bool staticAddr) { m_staticAddr = staticAddr; }
    uint8_t channel() const { return m_channel; }
    void setChannel(uint8_t channel) { m_channel = channel; }
    uint32_t channelMask() const { return m_channelMask; }
    void setChannelMask(uint32_t channelMask) { m_channelMask = channelMask; }
    uint8_t zigbeeVersion() const { return m_zigbeeVersion; }
    void setZigbeeVersion(uint8_t version) { m_zigbeeVersion = version; }
    uint8_t stackProfileId() const { return m_stackProfileId; }
    void setStackProfileId(uint8_t id) { m_stackProfileId = id; }
    uint8_t permitJoin() const { return m_permitJoin; }
    void setPermitJoin(uint8_t permitJoin) { m_permitJoin = permitJoin; }
    const QByteArray &networkKey() const { return m_networkKey; }
    void setNetworkKey(const QByteArray &key) { m_networkKey = key; }
    uint8_t networkKeySequenceNumber() const { return m_networkKeySequenceNumber; }
    void setNetworkKeySequenceNumber(uint8_t seqno) { m_networkKeySequenceNumber = seqno; }
    const QByteArray &trustCenterLinkKey() const { return m_tcLinkKey; }
    void setTrustCenterLinkKey(const QByteArray &key) { m_tcLinkKey = key; }
    const QByteArray &trustCenterMasterKey() const { return m_tcMasterKey; }
    void setTrustCenterMasterKey(const QByteArray &key) { m_tcMasterKey = key; }
    const QByteArray &zllKey() const { return m_zllKey; }
    void setZllKey(const QByteArray &key) { m_zllKey = key; }
    deCONZ::DeviceType deviceType() const { return m_deviceType; }
    void setDeviceType(deCONZ::DeviceType deviceType) { m_deviceType = deviceType; }
    uint8_t securityMode() const { return m_securityMode; }
    void setSecurityMode(uint8_t mode) { m_securityMode = mode; }
    uint8_t securityLevel() const { return m_securityLevel; }
    void setSecurityLevel(uint8_t level) { m_securityLevel = level; }
    bool useInsecureJoin() const { return m_useInsecureJoin; }
    void setUseInsecureJoin(bool insecureJoin) { m_useInsecureJoin = insecureJoin; }
    const std::vector<SecKeyPair> &securityKeyPairs() const { return m_keyPairs; }
    std::vector<SecKeyPair> &securityKeyPairs() { return m_keyPairs; }
    deCONZ::ConnectMode connectMode() const { return m_connMode; }
    void setConnectMode(deCONZ::ConnectMode mode) { m_connMode = mode; }
    bool zllFactoryNew() const { return m_zllFactoryNew; }
    void setZllFactoryNew(bool isNew) { m_zllFactoryNew = isNew; }
    uint8_t nwkUpdateId() const { return m_nwkUpdateId; }
    void setNwkUpdateId(uint8_t nwkUpdateId) { m_nwkUpdateId = nwkUpdateId; }

private:
    std::vector<SecKeyPair> m_keyPairs;
    deCONZ::Address m_tcAddr; //!< Trust center address.
    deCONZ::Address m_parent; //!< Parent address.
    deCONZ::Address m_ownAddr; //!< Our own address in this network.
    deCONZ::Address m_pan; //!< Current NWK PAN extended and short id.
    deCONZ::Address m_panAps; //!< Extended PANID the device should join.
    deCONZ::Address m_coord; //!< Coordinator address.
    QByteArray m_networkKey; //!< Network key (16 byte).
    QByteArray m_tcLinkKey; //!< Trust center link key (16 byte).
    QByteArray m_tcMasterKey; //!< Trust center master key (16 byte).
    QByteArray m_zllKey; //!< ZigBee Light Link key (16 byte).
    uint32_t m_channelMask = 0; //!< Channel mask.
    uint8_t m_nwkUpdateId = 0; //!< NWK update id.
    uint8_t m_networkKeySequenceNumber = 0; //! Network key sequence number.
    uint8_t m_channel = 0; //!< Logical channel.
    uint8_t m_zigbeeVersion = 2; //!< Zigbee version.
    uint8_t m_stackProfileId = 2; //!< Stack profile id.
    uint8_t m_securityMode = 0; //!< Security mode.
    uint8_t m_securityLevel = 0; //!< Security level.
    uint8_t m_permitJoin = 0; //!< // At least one router permits join.
    bool m_useInsecureJoin = false; //!< Use insecure join.
    bool m_predefPanid = false; //!< Use a predefined PANID.
    bool m_staticAddr = false; //!< Use a static NWK address.
    deCONZ::DeviceType m_deviceType = deCONZ::Coordinator;
    deCONZ::ConnectMode m_connMode = deCONZ::ConnectModeNormal;
    bool m_zllFactoryNew = false;
};

Q_DECLARE_METATYPE(zmNet)

#endif // NET_DESCRIPTOR_H
