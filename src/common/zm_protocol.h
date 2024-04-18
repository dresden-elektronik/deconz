/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_PROTOCOL_H_
#define ZM_PROTOCOL_H_

#include <stdint.h>

#define DECONZ_PROTOCOL_VERSION_MIN 0x0100UL // 1.0
#define DECONZ_PROTOCOL_VERSION_1_1 0x0101UL // 1.1
#define DECONZ_PROTOCOL_VERSION_1_2 0x0102UL // 1.2
#define DECONZ_PROTOCOL_VERSION_1_3 0x0103UL // 1.3
#define DECONZ_PROTOCOL_VERSION_1_4 0x0104UL // 1.4
#define DECONZ_PROTOCOL_VERSION_1_5 0x0105UL // 1.5
#define DECONZ_PROTOCOL_VERSION_1_6 0x0106UL // 1.6
#define DECONZ_PROTOCOL_VERSION_1_7 0x0107UL // 1.7
#define DECONZ_PROTOCOL_VERSION_1_8 0x0108UL // 1.8
#define DECONZ_PROTOCOL_VERSION_1_9 0x0109UL // 1.9
#define DECONZ_PROTOCOL_VERSION_1_10 0x010AUL // 1.10
#define DECONZ_PROTOCOL_VERSION_1_11 0x010BUL // 1.11
#define DECONZ_PROTOCOL_VERSION_1_12 0x010CUL // 1.12
#define DECONZ_PROTOCOL_VERSION_1_13 0x010DUL // 1.13
#define DECONZ_PROTOCOL_VERSION DECONZ_PROTOCOL_VERSION_1_11 // last

/*
 * Constant definitions
 */
#define ZM_HEADER_LENGTH               3
#define ZM_MAX_DATA_LENGTH             (sizeof(struct zm_command) - ZM_HEADER_LENGTH)
#define ZM_INVALID_CLUSTER             0xFFFF
#define ZM_MAX_BUFFER_LEN               116 // PDU length
#define ZM_GENERAL_FRAME_PREFIX_SIZE    (1 + 2 + 1) // seq + id + status
#define ZM_GENERAL_DATA_PREFIX_SIZE     (2 + 1) // id + options
#define ZM_MAX_GENRAL_DATA_SIZE          20

typedef enum
{
    ZM_STATE_SUCCESS      = 0x00,
    ZM_STATE_FAILURE      = 0x01,
    ZM_STATE_BUSY         = 0x02,
    ZM_STATE_TIMEOUT      = 0x03,
    ZM_STATE_UNSUPPORTED  = 0x04,
    ZM_STATE_ERROR        = 0x05,
    ZM_STATE_ENONET       = 0x06,
    ZM_STATE_EINVAL       = 0x07
} ZM_State_t;

// Status Byte 0
#define ZM_STATUS_NET_STATE_MASK  0x03
#define ZM_STATUS_APS_DATA_CONF   0x04
#define ZM_STATUS_APS_DATA_IND    0x08
#define ZM_STATUS_CONFIG_CHANGED  0x10
#define ZM_STATUS_FREE_APS_SLOTS  0x20

// Status Byte 1
#define ZM_STATUS_INTERPAN_MASK   0x18
#define ZM_STATUS_INTERPAN_IND    0x20
#define ZM_STATUS_INTERPAN_CONF   0x40

// ZM_COMMAND_BEGIN
enum ZM_COMMAND
{
    ZM_CMD_ACK                   = 0x00,
    ZM_CMD_INVALID               = 0x01,
    ZM_CMD_GENERAL               = 0x02,
    ZM_CMD_APS_DATA_REQ          = 0x03,
    ZM_CMD_APS_DATA_CONFIRM      = 0x04,
    ZM_CMD_APS_DATA_INDICATION   = 0x05,
    ZM_CMD_NPDU_INDICATION       = 0x06,
    ZM_CMD_STATUS                = 0x07,
    ZM_CMD_CHANGE_NET_STATE      = 0x08,
    ZM_CMD_ZDO_NET_CONFIRM       = 0x09,
    ZM_CMD_READ_PARAM            = 0x0A,
    ZM_CMD_WRITE_PARAM           = 0x0B,
    ZM_CMD_RESEND_LAST_CMD       = 0x0C,
    ZM_CMD_VERSION               = 0x0D,
    ZM_CMD_STATUS_CHANGE         = 0x0E, // same as ZM_CMD_STATUS but send without former request
    ZM_CMD_RESERVED8             = 0x0F,
    ZM_CMD_RESERVED9             = 0x10,
    ZM_CMD_FEATURE               = 0x11,
    ZM_CMD_APS_DATA_REQ_2        = 0x12,
    ZM_CMD_START_INTERPAN_MODE   = 0x13,
    ZM_CMD_SEND_INTERPAN_REQ     = 0x14,
    ZM_CMD_INTERPAN_INDICATION   = 0x15,
    ZM_CMD_INTERPAN_CONFIRM      = 0x16,
    ZM_CMD_APS_DATA_INDICATION_2 = 0x17,
    ZM_CMD_READ_REGISTER         = 0x18,
    ZM_CMD_GP_DATA_INDICATION    = 0x19,
    ZM_CMD_LINK_ADDRESS          = 0x1A,
    ZM_CMD_PHY_FRAME             = 0x1B,
    ZM_CMD_MAC_POLL              = 0x1C,
    ZM_CMD_UPDATE_NEIGHBOR       = 0x1D,
    ZM_CMD_REBOOT                = 0x1E,
    ZM_CMD_BEACON                = 0x1F,
    ZM_CMD_FACTORY_RESET         = 0x20,
    ZM_CMD_NWK_LEAVE_REQ         = 0x21,
    ZM_CMD_DEBUG_LOG             = 0x22,

    ZM_CMD_MAX = 0x23
};
// ZM_COMMAND_END

typedef enum
{
    ZM_CID_DATA_REQUEST                 = 0x0001,//!< Request data from device.
    ZM_CID_COMMAND_REQUEST              = 0x0002,//!< Request execution of a command
    ZM_CID_DATA_RESPONSE                = 0x8001,//!< Response to a data request.
    ZM_CID_COMMAND_RESPONSE             = 0x8002 //!< Response to a command request.
} ZM_CommandId_t;

// ZM_DID_BEGIN
typedef enum
{
    ZM_DID_MAC_ADDRESS                        = 0x01,//!< MIB macAddress.
    ZM_DID_NWK_SECURITY_LEVEL                 = 0x02,//!< NIB nwkSecurityLevel.
    ZM_DID_NWK_SECURITY_MATERIAL_SET          = 0x03,//!< NIB nwkSecurityMaterialSet.
    ZM_DID_NWK_ROUTER_AGE_LIMIT               = 0x04,//!< NIB nwkRouterAgeLimit.
    ZM_DID_NWK_PANID                          = 0x05,//!< NIB nwkPANId.
    ZM_DID_NWK_CAPABILITY_INFORMATION         = 0x06,//!< NIB nwkCapabilityInformation.
    ZM_DID_NWK_NETWORK_ADDRESS                = 0x07,//!< NIB nwkNetworkAddress.
    ZM_DID_NWK_EXTENDED_PANID                 = 0x08,//!< NIB nwkExtendedPANID.
    ZM_DID_APS_DESIGNED_COORDINATOR           = 0x09,//!< AIB apsDesignedCoordinator.
    ZM_DID_APS_CHANNEL_MASK                   = 0x0A,//!< AIB apsChannelMask.
    ZM_DID_APS_USE_EXTENDED_PANID             = 0x0B,//!< AIB apsUseExtendedPANID.
    ZM_DID_APS_PERMISSIONS_CONFIGURATION      = 0x0C,//!< AIB apsPermissionsConfiguration.
    ZM_DID_APS_USE_INSECURE_JOIN              = 0x0D,//!< AIB apsUseInsecureJoin.
    ZM_DID_APS_TRUST_CENTER_ADDRESS           = 0x0E,//!< AIB apsTrustCenterAddress.
    ZM_DID_APS_SECURITY_TIMEOUT_PERIOD        = 0x0F,//!< AIB apsSecurityTimeOutPeriod.
    ZM_DID_STK_SECURITY_MODE                  = 0x10,//!< STK stkSecurityMode.
    ZM_DID_STK_NETWORK_STATUS                 = 0x11,//!< STK stkNetworkStatus.
    ZM_DID_STK_DEBUG                          = 0x12,//!< STK stkDebug.
    ZM_DID_STK_ENDPOINT                       = 0x13,//!< STK stkEndpoint.
    ZM_DID_STK_PARAMETERS1                    = 0x14,//!< STK stkParameters1
    ZM_DID_STK_PREDEFINED_PANID               = 0x15,//!< NIB stkPredefinedPANID.
    ZM_DID_STK_STATIC_NETWORK_ADDRESS         = 0x16,//!< NIB stkStaticNetworkAddress.
    ZM_DID_STK_NETWORK_KEY_AMOUNT             = 0x17,//!< NIB stkNetworkKeyAmount.
    ZM_DID_STK_NETWORK_KEY                    = 0x18,//!< NIB stkNetworkKey.
    ZM_DID_STK_LINK_KEY                       = 0x19,//!< NIB stkLinkKey.
    ZM_DID_STK_TC_MASTER_KEY                  = 0x1A,//!< NIB stkTrustCenterMasterKey.
    ZM_DID_MAC_ADDRESS_CUSTOM                 = 0x1B,//!< NIB macAddressCustom.
    ZM_DID_STK_CURRENT_CHANNEL                = 0x1C,//!< STK stkCurrentChannel.
    ZM_DID_ZLL_KEY                            = 0x1D,//!< ZLL zllKey.
    ZM_DID_STK_CONNECT_MODE                   = 0x1E,//!< STK stkConnectMode.
    ZM_DID_STK_KEY_FOR_INDEX                  = 0x1F,//!< STK stkKeyForIndex.
    ZM_DID_ZLL_FACTORY_NEW                    = 0x20,//!< ZLL stkZllFactoryNew.
    ZM_DID_STK_PERMIT_JOIN                    = 0x21,//!< STK stkPermitJoin.
    ZM_DID_STK_PROTOCOL_VERSION               = 0x22,//!< STK stkProtocolVersion.
    ZM_DID_STK_ANT_CTRL                       = 0x23,//!< STK stkAntCtrl.
    ZM_DID_STK_NWK_UPDATE_ID                  = 0x24,//!< STK stkNwkUpdateId.
    ZM_DID_STK_SECURITY_MATERIAL0             = 0x25,//!< STK stkSecurityMaterial0.
    ZM_DID_DEV_WATCHDOG_TTL                   = 0x26,//!< DEV devWatchdogTtl.
    ZM_DID_STK_FRAME_COUNTER                  = 0x27,//!  STK stkFrameCounter.
    ZM_DID_STK_NO_ZDP_RESPONSE                = 0x28,//!< STK stkNoZdpResponse.
    ZM_DID_STK_DEBUG_LOG_LEVEL                = 0x29 //!< STK stkDebugLogLevel.
} ZM_DataId_t;
// ZM_DID_END

/*! ZM_DID_STK_ANT_CTRL values */
typedef enum
{
    ANTENNA_1_SELECT        = 0x01,
    ANTENNA_2_SELECT        = 0x02,
    ANTENNA_DEFAULT_SELECT  = 0x03
} AntennaSelect_t;

/*!
    Features which the generic device might support.
 */
typedef enum
{
    FEATURE_STD_SECURITY    = 0x01,
    FEATURE_LINK_SECURITY   = 0x02,
    FEATURE_HIGH_SECURITY   = 0x03,
    FEATURE_ETH             = 0x04,
    FEATURE_MSD             = 0x05,
    FEATURE_DFU             = 0x06,
    FEATURE_ZLL             = 0x07,
    FEATURE_INTERPAN        = 0x08,
    FEATURE_BUTTON_1        = 0x09,
    FEATURE_BUTTON_2        = 0x0a,
    FEATURE_LED_1           = 0x0b,
    FEATURE_LED_2           = 0x0c,
    FEATURE_LED_3           = 0x0d,
    FEATURE_LED_4           = 0x0e,
    FEATURE_LED_5           = 0x0f,
    FEATURE_LED_RGB         = 0x10,
    FEATURE_MAX_NODES       = 0x11
} FeatureSet1_t;

typedef enum
{
    ZM_STANDARD_NETWORK_KEY      = 0x01,
    ZM_APPLICATION_LINK_KEY      = 0x02,
    ZM_MASTER_KEY                = 0x03,
    ZM_TRUST_CENTER_LINK_KEY     = 0x04,
    ZM_HIGH_SECURITY_NETWORK_KEY = 0x05
} ZM_KeyType_t;

typedef enum
{
    // following related to BitClouds CS_ZDO_SECURITY_STATUS

    ZM_NO_SECURITY                     = 0x00,
    ZM_STD_PRECONFIGURED_NETWORK_KEY   = 0x01, // BC mode 0
    ZM_STD_NETWORK_KEY_FROM_TC         = 0x02, // BC mode 3
    ZM_HIGH_NO_MASTER_BUT_TC_LINK_KEY  = 0x03, // BC mode 1
    ZM_HIGH_WITH_MASTER_KEY            = 0x04  // BC mode 2
} ZM_SecurityMode_t;

typedef enum
{
    ZM_DATA_OPTION_NONE                = 0x00,
    ZM_DATA_OPTION_STORE_PERSITENT     = 0x01,
    ZM_DATA_OPTION_RESTORE_DEFAULT     = 0x02,
    ZM_DATA_OPTION_READ                = 0x04,
    ZM_DATA_OPTION_WRITE               = 0x08
} ZM_DataOptions_t;

enum
{
    ZM_APS_REQUEST_KEY              = 0x01,
    ZM_APS_REQUEST_DATA_REQUEST     = 0x30,
    ZM_APS_REQUEST_DATA_CONFIRM     = 0x31,
    ZM_APS_REQUEST_DATA_INDICATION  = 0x32
};

typedef enum
{
    ZM_NET_OFFLINE = 0x00,
    ZM_NET_JOINING = 0x01,
    ZM_NET_ONLINE  = 0x02,
    ZM_NET_LEAVING = 0x03
} ZM_NetState_t;

typedef enum
{
    ZLL_NET_NOT_CONNECTED  = 0x00,
    ZLL_NET_TOUCHLINK      = 0x01,
    ZLL_NET_CONNECTED      = 0x02
} ZLL_NetState_t;

typedef enum
{
    IPAN_NOT_CONNECTED  = 0x00,
    IPAN_CONNECTING     = 0x01,
    IPAN_CONNECTED      = 0x02
} IPAN_State_t;

typedef struct zm_buffer
{
    uint16_t len;
    uint8_t data[ZM_MAX_BUFFER_LEN];
} ProtBuf_t;

/*!
    General device frame.

    Transferred in a ::zm_buffer.
 */
struct zm_general_frame
{
    uint8_t seq; //!< The sequence number.
    uint16_t id; //!< A ::ZM_CommandId_t.
    uint8_t status; //!< A ::ZM_State_t.
    uint8_t  data[1];
};

/*!
    General data frame used to get and set data.
 */
struct zm_general_data
{
  uint16_t id; //!< A :: ZM_DataId_t.
  uint8_t options; //!< OR combined ::ZM_DataOptions_t flags.

  union
  {
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    uint8_t data[1];
  } param;
};


/*!
    The general command frame.

    Used by master and slave.
 */
struct zm_command
{
    unsigned char cmd; //!< Command id which is one of the ZM_CMD_* values.
    unsigned char seq; //!< Sequence number
    unsigned char status; //!< Status

    union
    {
        uint8_t _sequence_begin; //!< \internal only used as marker

        uint8_t data[ZM_MAX_BUFFER_LEN];
        struct zm_buffer buffer;
        struct zm_general_frame general;
    };
};

typedef enum zm_parse_status
{
    ZM_PARSE_OK = 0,
    ZM_PARSE_ERROR_WRONG_STORED_LENGTH = 1,
    ZM_PARSE_ERROR_CHAR_FIELD_OVERFLOW = 2,
    ZM_PARSE_ERROR_DBUF_LEN_LARGER_DATA = 3,
    ZM_PARSE_ERROR_DBUF_LEN_TOO_LARGE = 4,
    ZM_PARSE_ERROR_DBUF_INCOMPLETE = 5,
    ZM_PARSE_ERROR_UNKNOWN_FIELD = 6,
    ZM_PARSE_ERROR_NO_FIELDS = 7,
    ZM_PARSE_ERROR_UNKNOWN_COMMAND = 8,
    ZM_PARSE_ERROR_COMMAND_BUF_TOO_SMALL = 9,
    ZM_PARSE_ERROR_READ_OVERFLOW = 10
} zm_parse_status_t;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Prototypes
 */
const char* protocol_strstate(ZM_State_t state);
zm_parse_status_t zm_protocol_buffer2command(const uint8_t *data, const uint16_t len, struct zm_command *cmd);
uint16_t zm_protocol_command2buffer(struct zm_command *cmd, uint16_t version, uint8_t *buf, uint16_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* ZM_PROTOCOL_H_ */
