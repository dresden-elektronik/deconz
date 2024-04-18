/**
 * $Id: protocol.h,v 1.1 2010/08/20 12:19:01 ml Exp $
 *
 * @file
 * \todo add a description here
 *
 * $Revision: 1.1 $
 * $Date: 2010/08/20 12:19:01 $
 *
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H


#define PROTOCOL_VERSION_MAJOR 1
#define PROTOCOL_VERSION_MINOR 0
#define PROTOCOL_VERSION_MAINTENANCE 1

/*
 * Constant definitions
 */
#define PROTO_RX                          (0x02)  // receive enabled
#define PROTO_TX                          (0x04)  // transmit enabled
#define PROTO_TX_ON_RX                    (0x08)  // transmit auto enabled on receive
#define PROTO_FLAGGED                     (0x10)  // the protocol uses flagging for packets
#define PROTO_FIXED                       (0x20)  // the protocol uses fixed size packets
#define PROTO_TRACE                       (0x80)  // trace this protocol on low level
#define PROTO_NO_PROTOCOL                 (0xFF)
#define PROTO_OVERHEAD_LEN                16      // byte count of the protocol overhead

/*
 * Type definitions
 */
typedef char  (* tGetCFN)(void);
typedef char  (* tIsCFN)(void);
typedef short (* tPutCFN)(char c);
typedef void  (* tPacketFN)(unsigned char* pData, unsigned short u16Len);

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Prototypes
 */
void protocol_init(void);
void protocol_exit(void);
unsigned char protocol_add(unsigned char u8Options, tGetCFN pGetC, tIsCFN pIsC, tPutCFN pPutC, void (*flush)(void),tPacketFN pPacket);
unsigned char protocol_remove(unsigned char u8Instance);
unsigned char protocol_set_buffer(unsigned char u8Instance, unsigned char* pBuffer, unsigned short u16Len);
void protocol_send(unsigned char u8Instance, unsigned char* pData, unsigned short u16Len);
void protocol_receive(unsigned char u8Instance);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PROTOCOL_H */

