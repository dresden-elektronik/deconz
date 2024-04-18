#include <stdint.h>
#include "crc8.h"

/*!
    CRC-8 Dallas/Maxim
    Counts crc current memory area. CRC-8. Polynom 0x31    x^8 + x^5 + x^4 + 1.

\param[in]
  crc - first crc state
\param[in]
  data - pointer to the memory for crc counting
\param[in]
  length - memory size

\return
  current area crc
 */
uint8_t CRC8_Dallas(uint8_t crc, uint8_t *data, uint16_t length)
{
  uint8_t i;

  while (length--)
  {
    crc ^= *data++;

    for (i = 0; i < 8; i++)
      crc = crc & 0x80 ? (crc << 1) ^ 0x31 : crc << 1;
  }

  return crc;
}
