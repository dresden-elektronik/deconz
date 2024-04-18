/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "deconz/buffer_helper.h"
#include "common/zm_protocol.h"

/*! String mapping for ZM_State_t enum. */
static const char *strstate[] =
{
    "SUCCESS",
    "FAILURE",
    "BUSY",
    "TIMEOUT",
    "UNSUPPORTED",
    "ERROR",
    "ENONET",
    "EINVAL",
    NULL
};

struct zm_type_descr
{
    uint8_t cmd; /*!< a ZM_CMD_ command */

    /*!
        A generic payload description sequence.

        Each character stands for a c type:

        c     8 bit unsigned char
        D    (u16 length, char[]) dynamic buffer

        A valid list always ends with a '\0' if the list is NULL
        that means 'not implemented yet'.
     */
    const char* fields;
};

static const struct zm_type_descr Type_table[ZM_CMD_MAX] = {
        [ZM_CMD_ACK]                    = { ZM_CMD_ACK,                   NULL },
        [ZM_CMD_INVALID]                = { ZM_CMD_INVALID,               NULL },
        [ZM_CMD_GENERAL]                = { ZM_CMD_GENERAL,               "D" },
        [ZM_CMD_APS_DATA_REQ]           = { ZM_CMD_APS_DATA_REQ,          "D" },
        [ZM_CMD_APS_DATA_CONFIRM]       = { ZM_CMD_APS_DATA_CONFIRM,      "D" },
        [ZM_CMD_APS_DATA_INDICATION]    = { ZM_CMD_APS_DATA_INDICATION,   "D" },
        [ZM_CMD_NPDU_INDICATION]        = { ZM_CMD_NPDU_INDICATION,       "D" },
        [ZM_CMD_STATUS]                 = { ZM_CMD_STATUS,                "ccc" }, // also versions with cc deployed
        [ZM_CMD_CHANGE_NET_STATE]       = { ZM_CMD_CHANGE_NET_STATE,      "c" },
        [ZM_CMD_ZDO_NET_CONFIRM]        = { ZM_CMD_ZDO_NET_CONFIRM,       "c" },
        [ZM_CMD_READ_PARAM]             = { ZM_CMD_READ_PARAM,            "D" },
        [ZM_CMD_WRITE_PARAM]            = { ZM_CMD_WRITE_PARAM,           "D" },
        [ZM_CMD_RESEND_LAST_CMD]        = { ZM_CMD_RESEND_LAST_CMD,       "cc" },
        [ZM_CMD_VERSION]                = { ZM_CMD_VERSION,               "cccc" },
        [ZM_CMD_STATUS_CHANGE]          = { ZM_CMD_STATUS_CHANGE,         "cc" },
        [ZM_CMD_RESERVED8]              = { ZM_CMD_RESERVED8,             NULL },
        [ZM_CMD_RESERVED9]              = { ZM_CMD_RESERVED9,             NULL },
        [ZM_CMD_FEATURE]                = { ZM_CMD_FEATURE,               "D" },
//---------- protocol version 1.1 --------------------
        [ZM_CMD_APS_DATA_REQ_2]         = { ZM_CMD_APS_DATA_REQ_2,        "D" },
//---------- protocol version 1.2 --------------------
        [ZM_CMD_START_INTERPAN_MODE]    = { ZM_CMD_START_INTERPAN_MODE,   "D" },
        [ZM_CMD_SEND_INTERPAN_REQ]      = { ZM_CMD_SEND_INTERPAN_REQ,     "D" },
        [ZM_CMD_INTERPAN_INDICATION]    = { ZM_CMD_INTERPAN_INDICATION,   "D" },
        [ZM_CMD_INTERPAN_CONFIRM]       = { ZM_CMD_INTERPAN_CONFIRM,      "D" },
        [ZM_CMD_APS_DATA_INDICATION_2]  = { ZM_CMD_APS_DATA_INDICATION_2, "D" },
        [ZM_CMD_READ_REGISTER]          = { ZM_CMD_READ_REGISTER,         "D" },
        [ZM_CMD_GP_DATA_INDICATION]     = { ZM_CMD_GP_DATA_INDICATION,    "D" },
//---------- protocol version 1.3 --------------------
        [ZM_CMD_LINK_ADDRESS]           = { ZM_CMD_LINK_ADDRESS,          "D" },
//---------- protocol version 1.4 --------------------
        [ZM_CMD_PHY_FRAME]              = { ZM_CMD_PHY_FRAME,             "D" },
//---------- protocol version 1.5 --------------------
        [ZM_CMD_MAC_POLL]               = { ZM_CMD_MAC_POLL,              "D" },
        [ZM_CMD_UPDATE_NEIGHBOR]        = { ZM_CMD_UPDATE_NEIGHBOR,       "D" },
//---------- protocol version 1.6 --------------------
        [ZM_CMD_REBOOT]                 = { ZM_CMD_REBOOT,                "" },
//---------- protocol version 1.9 --------------------
        [ZM_CMD_BEACON]                 = { ZM_CMD_BEACON,                "D" },
//---------- protocol version 1.10 -------------------
        [ZM_CMD_FACTORY_RESET]          = { ZM_CMD_FACTORY_RESET,         "" },
//---------- protocol version 1.11 -------------------
        [ZM_CMD_NWK_LEAVE_REQ]           = { ZM_CMD_NWK_LEAVE_REQ,        "D" },
        [ZM_CMD_DEBUG_LOG]               = { ZM_CMD_DEBUG_LOG,        "D" }
};

/*
 * Local prototypes
 */
static const uint8_t *get_u16(const uint8_t* p, uint16_t* p16);

const char* protocol_strstate(ZM_State_t state)
{
    switch (state)
    {
    case ZM_STATE_SUCCESS:
    case ZM_STATE_FAILURE:
    case ZM_STATE_BUSY:
    case ZM_STATE_TIMEOUT:
    case ZM_STATE_UNSUPPORTED:
    case ZM_STATE_ERROR:
    case ZM_STATE_ENONET:
    case ZM_STATE_EINVAL:
        return strstate[state];

    default:
        break;
    }

    return "unknown";
}

zm_parse_status_t zm_protocol_buffer2command(const uint8_t *data, const uint16_t len, struct zm_command *cmd)
{
    if (data && (len >= ZM_HEADER_LENGTH) && cmd)
    {
        cmd->cmd = data[0];
        cmd->seq = data[1];
        cmd->status = data[2];
    }
    else
    {
       return ZM_PARSE_ERROR_COMMAND_BUF_TOO_SMALL;
    }

    if (cmd->cmd < ZM_CMD_MAX && Type_table[cmd->cmd].cmd == cmd->cmd && len >= 5) // known command and stored_length field present
    {
        uint16_t stored_len;
        const uint8_t *p = get_u16(&data[3], &stored_len); // length

        if (len != stored_len)
        {
            return ZM_PARSE_ERROR_WRONG_STORED_LENGTH;
        }

        const char *fp = Type_table[cmd->cmd].fields; // field pointer
        uint8_t *wp = &cmd->_sequence_begin; // write pointer

        if (fp == NULL)
        {
            return ZM_PARSE_ERROR_NO_FIELDS;
        }

        for (; *fp; fp++)
        {
            switch (*fp)
            {
            case 'c':
                if (p + 1 <= data + len)
                {
                    *wp++ = *p++;
                }
                else if (cmd->cmd == ZM_CMD_STATUS)
                {
                    // fill with zero if expected more
                    // workaround for STATUS command which has cc and ccc in the filed
                    *wp++ = 0;
                }
                else
                {
                    return ZM_PARSE_ERROR_CHAR_FIELD_OVERFLOW;
                }
                break;

            case 'D':
                if (p + 2 <= data + len)
                {
                    p = get_u16(p, &cmd->buffer.len);

                    if ((p + cmd->buffer.len) > (data + len))
                    {
                        return ZM_PARSE_ERROR_DBUF_LEN_LARGER_DATA;
                    }

                    if (cmd->buffer.len > (sizeof(cmd->buffer) - sizeof(cmd->buffer.len)))
                    {
                        return ZM_PARSE_ERROR_DBUF_LEN_TOO_LARGE;
                    }

                    for (uint8_t i = 0; i < cmd->buffer.len; i++)
                    {
                        cmd->buffer.data[i] = *p++;
                    }
                }
                else
                {
                    return ZM_PARSE_ERROR_DBUF_INCOMPLETE;
                }
                break;

            default:
                return ZM_PARSE_ERROR_UNKNOWN_FIELD;
            }

            if (p > (data + len))
            {
                return ZM_PARSE_ERROR_READ_OVERFLOW;
            }
        }

        return ZM_PARSE_OK;
    }
    else
    {
        return ZM_PARSE_ERROR_UNKNOWN_COMMAND;
    }
}

uint16_t zm_protocol_command2buffer(struct zm_command *cmd, uint16_t version, uint8_t *buf, uint16_t max_len)
{
    uint8_t *pb;  // data pointer
    const char *fp; // field pointer
    uint8_t *rp; // read pointer

    (void)version;

    if (cmd && buf)
    {
        if (max_len >= ZM_HEADER_LENGTH)
        {
            buf[0] = cmd->cmd;
            buf[1] = cmd->seq;
            buf[2] = cmd->status;
            pb = buf + 3;

            if (cmd->cmd == ZM_CMD_ACK)
            {
                return 3;
            }
            else if (cmd->cmd < ZM_CMD_MAX)
            {
                // save two bytes: length will be written here later
                *pb++ = 0xCD;
                *pb++ = 0xAB;

                fp = Type_table[(int)cmd->cmd].fields;
                rp = &cmd->_sequence_begin;

                if (fp != NULL)
                {
                    for (; *fp; fp++)
                    {
                        if ((pb - buf) >= max_len)
                        {
//                            dbg_printf_p((DSTR("ZM: Skip command with id = %d, buffer overflow detected"), cmd->cmd));
                            return 0;
                        }

                        switch (*fp)
                        {
                        case 'c': pb = put_u8_le(pb, rp); rp++; break;
                        case 'h': memcpy(pb, rp, 2); pb += 2; rp += 2; break;
                        case 'i': memcpy(pb, rp, 4); pb += 4; rp += 4; break;
                        case 'e': memcpy(pb, rp, 8); pb += 8; rp += 8; break;
                        case 's':
                            while (*rp)
                            {
                                *pb++ = *rp++;
                            }
                            *pb++ = *rp++; /* '\0' */
                            break;

                        case 'B':
                            {
                                max_len = (uint16_t)*rp;

                                /* store len */
                                *pb++ = *rp++;

                                while (max_len > 0)
                                {
                                    *pb++ = *rp++;
                                    max_len--;
                                }
                            }
                            break;

                        case 'H':
                            {
                                max_len = (uint16_t)*rp;

                                /* store len */
                                *pb++ = *rp++;

                                while (max_len > 0)
                                {
                                    memcpy(pb, rp, 2);
                                    pb += 2;
                                    rp += 2;
                                    max_len--;
                                }
                            }
                            break;

                        case 'D':
                            {
                                if ((cmd->buffer.len > (sizeof(cmd->buffer) - sizeof(cmd->buffer.len))))
                                {
//                                    dbg_printf_p((DSTR("ZM: Skip command with id = %d, buffer overflow detected"), cmd->cmd));
                                    return 0;
                                }
                                pb = put_u16_le(pb, &cmd->buffer.len);
                                memcpy(pb, cmd->buffer.data, cmd->buffer.len);
                                pb += cmd->buffer.len;
                            }
                            break;

                        default:
                            break;
                        }
                    }

                    const uint16_t len = (uint16_t)(pb - buf);

                    put_u16_le(&buf[3], &len); // store length

                    return len; // written length
                }
                else
                {
//                    dbg_printf_p((DSTR("ZM: Skip command with id = %d, unknown fields"), cmd->cmd));
                }
            }
        }
        else
        {
//            dbg_printf_p((DSTR("N3: buffer too small for any command - at least %d bytes"), ZM_HEADER_LENGTH));
        }
    }

    return 0;
}

// from LE
static const uint8_t* get_u16(const uint8_t* p, uint16_t* p16)
{
    *p16 = *p;
    *p16 |= p[1] << 8;
    return p + 2;
}
