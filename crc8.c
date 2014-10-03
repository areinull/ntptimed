#include "crc8.h"

/*---------------------------------------------------------------------------*/
u8
crc8 (u8 crc,                   /* initialization value */
      u8 xor_out,               /* finalization value */
      const char *data,         /* data buffer */
      int len                   /* size of buffer */
    )
{
    while (len--)
        crc = crc8_31_table[crc ^ *data++];
    crc ^= xor_out;

    return crc;
}

/*---------------------------------------------------------------------------*/

u8
crc8_sr (u8 crc,                /* initialization value */
         u8 xor_out,            /* finalization value */
         u8 polinom,            /* polinom */
         const char *data,      /* data buffer */
         int len                /* size of buffer */
    )
{
    u8 i;
    while (len--)
      {
          crc ^= *data++;

          for (i = 0; i < 8; i++)
              crc = crc & 0x80 ? (crc << 1) ^ polinom : crc << 1;
      }
    crc ^= xor_out;

    return crc;
}

/*---------------------------------------------------------------------------*/

u8
crc8_31_ff (u8 * data,          /*  data buffer  */
            u8 len              /*  size of buffer  */
    )
{
    u8 crc = 0xff;
    while (len--)
        crc = crc8_31_table[crc ^ *data++];

    return crc;
}

/*---------------------------------------------------------------------------*/
