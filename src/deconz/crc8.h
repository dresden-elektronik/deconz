#ifndef _CRC8_CALCS
#define _CRC8_CALCS

#ifdef __cplusplus
extern "C" {
#endif

uint8_t CRC8_Dallas(uint8_t crc, uint8_t *data, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif // _CRC8_CALCS
