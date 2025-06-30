/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include "common/protocol.h"

/*
 * Constant definitions
 */
#define PROTO_MAX_DEV   1

// define the communication flags
#define FR_END       (unsigned char)0xC0
#define FR_ESC       (unsigned char)0xDB
#define T_FR_END     (unsigned char)0xDC
#define T_FR_ESC     (unsigned char)0xDD
#define ASC_FLAG     0x01

#define PACKET_SYNC  0xAA
#define PACKET_LEN   0x16


// Our send structure:
//   In flagging mode
// we use a flagging method with an END flag and an ESC flag
// each byte equal to the END or ESC flag within the data are
// flagged and a frame and a unflagged END is used as a sync
// byte, that is, if the receiver gets a unflagged END byte the
// receiver is flushed and everything received is discarded
// additionally we send a crc as the last two bytes of a frame
//
// a frame always looks like
//
//  END | tCommand | CRC low | CRC high | END
// the crc is the sum of all byte unflagged and without the crc itself
// the low byte is send as the complement + 1, and the high byte is send as
// is with a one added
// the receiver just looks at frame end for the last 2 byte and both should
// be equal to the high byte of its own calculated crc
//
//
//   In fixed mode
// each frame has a fixed length always starting with an sync byte and a
// length field followed by some data and an crc
//
// a frame always looks like
//
// SYNC | LEN | data | CRC
// the crc is the sum of all byte without the crc itself and made negative (-sum)
//

/*
 * Local type definitions
 */
typedef struct stProtocol_s
{
  unsigned char   u8Escaped;
  unsigned char   u8Options;
  tGetCFN         pGetC;
  tIsCFN          pIsC;
  tPutCFN         pPutC;
  void (*pFlush)(void);
  tPacketFN       pPacket;
  unsigned char*  pBuffer;
  unsigned short  u16BufferLen;
  unsigned short  u16BufferPos;

} tProtocol;

/*
 * Local variables
 */
static unsigned char bInit = 0;
static tProtocol arDevices[PROTO_MAX_DEV];

/*
 * Local prototypes
 */
static void protocol_send_flagged(tProtocol* pDev, unsigned char* pData, unsigned short u16Len);
static void protocol_receive_flagged(tProtocol* pDev);

/*****************************************************************************/
/**
  * Initialize the PROTOCOL module
  *
  *
  * @param         void
  * @return        void
  *
  * @author
  * @date          05.05.2006 11:23
  *
  *****************************************************************************/
void protocol_init(void)
{
   unsigned int i;

   for (i = 0; i < PROTO_MAX_DEV; i++)
   {
      arDevices[i].u8Options = 0;
      arDevices[i].u8Escaped = 0;
      arDevices[i].pGetC = 0;
      arDevices[i].pIsC = 0;
      arDevices[i].pPutC = 0;
      arDevices[i].pPacket = 0;

      arDevices[i].pBuffer = 0;
      arDevices[i].u16BufferLen = 0;
      arDevices[i].u16BufferPos = 0;
   }

   bInit = 1;
}

/*****************************************************************************/
/**
  * Close the PROTOCOL module
  *
  *
  * @param         void
  * @return        void
  *
  * @author
  * @date          05.05.2006 11:23
  *
  *****************************************************************************/
void protocol_exit(void)
{
   unsigned int i;

   for (i = 0; i < PROTO_MAX_DEV; i++)
   {
      arDevices[i].u8Options = 0;
   }

   bInit = 0;
}

/*****************************************************************************/
/**
  * Add a device to the PROTOCOL module
  *
  *
  * @param         unsigned char      Options
  * @param         tGetCFN    the GetChar function of the device
  * @param         tIsKeyFN   the IsKey function of the device
  * @param         tPutCFN    the PutChar function of the device
  * @return        unsigned char      the instance handle on success, PROTO_NO_PROTOCOL else
  *
  * @author
  * @date          05.05.2006 11:23
  *
  *****************************************************************************/
unsigned char protocol_add(unsigned char u8Options, tGetCFN pGetC, tIsCFN pIsC, tPutCFN pPutC, void (*flush)(void), tPacketFN pPacket)
{
   unsigned char i;

   // get a free device
   for (i = 0; i < PROTO_MAX_DEV; i++)
   {
      if (arDevices[i].u8Options == 0)
      {
         if (pGetC && pIsC && pPutC && pPacket && (u8Options != 0))
         {
            arDevices[i].u8Options = u8Options;
            arDevices[i].u8Escaped = 0;

            arDevices[i].pBuffer = 0;
            arDevices[i].u16BufferLen = 0;
            arDevices[i].u16BufferPos = 0;

            arDevices[i].pGetC = pGetC;
            arDevices[i].pIsC = pIsC;
            arDevices[i].pPutC = pPutC;
            arDevices[i].pFlush = flush;
            arDevices[i].pPacket = pPacket;
            return i;
         }
         break;
      }
   }
   return PROTO_NO_PROTOCOL;
}

/*****************************************************************************/
/**
  * Remove a device from the PROTOCOL module
  *
  *
  * @param         unsigned char      the instance to remove
  * @return        unsigned char      1 on succes, 0 else
  *
  * @author
  * @date          05.05.2006 11:23
  *
  *****************************************************************************/
unsigned char protocol_remove(unsigned char u8Instance)
{
   if (bInit && (u8Instance < PROTO_MAX_DEV))
   {
      arDevices[u8Instance].u8Options = 0;
      return 1;
   }
   return 0;
}

/*****************************************************************************/
/**
  * Set the receive buffer for a device
  *
  *
  * @param         unsigned char      the instance to set the buffer for
  * @param         unsigned char*     the pointer to the buffer
  * @param         unsigned char      the size of the buffer
  * @return        unsigned char      1 on success
  *
  * @author
  * @date          05.05.2006 11:23
  *
  *****************************************************************************/
unsigned char protocol_set_buffer(unsigned char u8Instance, unsigned char* pBuffer, unsigned short u16Len)
{
   if (bInit && (u8Instance < PROTO_MAX_DEV))
   {
      if (pBuffer && (u16Len > 0))
      {
         arDevices[u8Instance].pBuffer = pBuffer;
         arDevices[u8Instance].u16BufferLen = u16Len;
      }
      else
      {
         arDevices[u8Instance].pBuffer = 0;
         arDevices[u8Instance].u16BufferLen = 0;
      }

      arDevices[u8Instance].u16BufferPos = 0;
      return 1;
   }
   return 0;
}

/*****************************************************************************/
/**
  * send a binary data packet - use escape technique and apply a crc
  *
  *
  * @param         unsigned char the protocol instance to send a packet for
  * @param         unsigned char* pointer to the data buffer to send
  * @return        void
  *
  * @author
  * @date          05.05.2006 11:23
  *
  *****************************************************************************/
void protocol_send(unsigned char u8Instance, unsigned char* pData, unsigned short u16Len)
{
   if (bInit && (u8Instance < PROTO_MAX_DEV) && pData && (u16Len > 0))
   {
      if (arDevices[u8Instance].u8Options & PROTO_TX)
      {
         tProtocol* pDev = &arDevices[u8Instance];
         protocol_send_flagged(pDev, pData, u16Len);
         if (pDev->pFlush)
         {
             pDev->pFlush();
         }
      }
   }
}

/*****************************************************************************/
/**
  * receive a binary data packet - use escape technique and check the crc
  *
  *
  * @param         unsigned char      the the device instance
  * @return        void
  *
  * @author
  * @date          05.05.2006 11:23
  *
  *****************************************************************************/
void protocol_receive(unsigned char u8Instance)
{
   if (bInit && (u8Instance < PROTO_MAX_DEV))
   {
	  tProtocol* pDev = &arDevices[u8Instance];
      if (pDev->u8Options & PROTO_RX)
      {
         protocol_receive_flagged(pDev);
      }
   }
}

/*****************************************************************************/
/**
  * receive a binary data packet - use escape technique and check the crc
  *
  *
  * @param         unsigned char      the the device instance
  * @return        void
  *
  * @author
  * @date          05.05.2006 11:23
  *
  *****************************************************************************/
static void protocol_receive_flagged(tProtocol* pDev)
{
   unsigned i;
   unsigned char c;

   do
   {
      c = pDev->pGetC();

      switch (c)
      {
      case FR_END:
         if (pDev->u8Escaped)
         {
            pDev->u16BufferPos = 0;
            pDev->u8Escaped &= ~ASC_FLAG;
         }
         else
         {
            if (pDev->u16BufferPos >= 2)
            {
                unsigned short crc;
                unsigned short crcFrame;

                crc = 0;
                for (i = 0; i < pDev->u16BufferPos - 2; i++)
                    crc += pDev->pBuffer[i];

                crc = (~crc + 1);
                crcFrame = pDev->pBuffer[pDev->u16BufferPos - 1];
                crcFrame <<= 8;
                crcFrame |= pDev->pBuffer[pDev->u16BufferPos - 2];

                if (crc == crcFrame)
                {
                    if (pDev->pPacket)
                    {
                        pDev->pPacket(&pDev->pBuffer[0], (unsigned short)(pDev->u16BufferPos - 2));
                    }
                }
            }

            pDev->u16BufferPos = 0;
         }
         return;
      case FR_ESC:
         pDev->u8Escaped |= ASC_FLAG;
         return;
      }

      if (pDev->u8Escaped & ASC_FLAG)
      {
         // translate the 2 byte escape sequence back to original char
         pDev->u8Escaped &= ~ASC_FLAG;

         switch (c)
         {
         case T_FR_ESC: c = FR_ESC; break;
         case T_FR_END: c = FR_END; break;
         default: // TODO(mpi) this is an error
             return;
         }
      }

      // we reach here with every byte for the buffer
      if (pDev->pBuffer && (pDev->u16BufferPos < pDev->u16BufferLen))
      {
         pDev->pBuffer[pDev->u16BufferPos++] = c;
      }
   }
   while(pDev->pIsC());
}

/*****************************************************************************/
/**
  * send a binary data packet - use escape technique and apply a crc
  *
  *
  * @param         tProtocol* the protocol instance to send a packet for
  * @param         unsigned char* pointer to the data buffer to send
  * @return        void
  *
  * @author
  * @date          05.05.2006 11:23
  *
  *****************************************************************************/
static void protocol_send_flagged(tProtocol* pDev, unsigned char* pData, unsigned short u16Len)
{
   unsigned char c = 0;
   unsigned short i = 0;
   unsigned short u16Crc = 0;

   // put an end before the packet
   pDev->pPutC(FR_END);

   while (i < u16Len)
   {
      c = pData[i++];
      u16Crc += c;

      switch (c)
      {
      case FR_ESC:
         pDev->pPutC(FR_ESC);
         pDev->pPutC(T_FR_ESC);
         break;
      case FR_END:
         pDev->pPutC(FR_ESC);
         pDev->pPutC(T_FR_END);
         break;
      default:
         pDev->pPutC(c);
         break;
      }
   }

   c = (~u16Crc + 1) & 0xFF;
   if (c == FR_ESC)
   {
      pDev->pPutC(FR_ESC);
      pDev->pPutC(T_FR_ESC);
   }
   else if (c == FR_END)
   {
      pDev->pPutC(FR_ESC);
      pDev->pPutC(T_FR_END);
   }
   else
   {
      pDev->pPutC(c);
   }

   c = ( (~u16Crc + 1) >> 8)   & 0xFF;
   if (c == FR_ESC)
   {
      pDev->pPutC(FR_ESC);
      pDev->pPutC(T_FR_ESC);
   }
   else if (c == FR_END)
   {
      pDev->pPutC(FR_ESC);
      pDev->pPutC(T_FR_END);
   }
   else
   {
      pDev->pPutC(c);
   }

   // tie off the packet
   pDev->pPutC(FR_END);
}
