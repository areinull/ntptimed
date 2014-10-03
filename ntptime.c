#include <arpa/inet.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include "ntptime.h"
#include "crc8.h"

int ntptime_send(GlinkContext *link,const ntptime_t *time)
{
        u8 buf[10];
        u32 tmp;
        buf[0]=0xff;

        /*  convert to big endian  */
        tmp=htonl(time->sec);
        memcpy(buf+1, &tmp, 4);
        tmp=htonl(time->psec);
        memcpy(buf+5, &tmp, 4);

        buf[9]=crc8_31_ff(buf+1,8);

        glink_send_buf(link, (char*)buf, 10);

        return 0;
}

int ntptime_get(GlinkContext *link, ntptime_t *time, ntptime_t *localtime,
                int (*localtime_get)(ntptime_t*) )
{
        u8 buf[10];
        u32 tmp;

        if (link->type == UDP_GLINK)
        {
          glink_get_buf(link, (char*)buf, 10);
          if( buf[0]!=0xff )
              return NTPTIMEERR_NOFF;

          /* check crc */
          if( (crc8_31_ff(&buf[1],8))!=buf[9] )
              return NTPTIMEERR_CRC;

          /* measure localtime */
          if( (*localtime_get)(localtime) )
              return NTPTIMEERR_TLOCAL;

          /* get time */
          memcpy(&tmp, buf+1, 4);
          time->sec=ntohl(tmp);
          memcpy(&tmp, buf+5, 4);
          time->psec=ntohl(tmp);
        }
        else
        {
          int bytesReq = 9;  /* need to receive 9 bytes, 0xFF excluded */
          int res;
          if (glink_get_byte(link) != 0xff)
            return NTPTIMEERR_NOFF;
          /* measure localtime */
          if ((*localtime_get)(localtime))
            return NTPTIMEERR_TLOCAL;
          while (bytesReq)
          {
            res = glink_get_buf(link, (char*)buf+9-bytesReq, bytesReq);
            if (res <= 0)  /* 0 means timeout, so quit */
              return NTPTIMEERR_IO;
            bytesReq -= res;
          }
          /* check crc */
          if ((crc8_31_ff(buf, 8)) != buf[8])
            return NTPTIMEERR_CRC;
          /* get time */
          memcpy(&tmp, buf, 4);
          time->sec=ntohl(tmp);
          memcpy(&tmp, buf+4, 4);
          time->psec=ntohl(tmp);
        }

        return 0;
}


int localtime_get_gtd(ntptime_t *localtime)
{
    struct timeval tv;

    if( gettimeofday(&tv,NULL)<0 )
      return -1;
    localtime->sec=tv.tv_sec+NTP_UNIX_DELTA;
    /* fraction part = quantity of the least possible number (2^-32) */
    localtime->psec=(u32)((double)tv.tv_usec*4294.967296);

    return 0;
}

int ntptime_shm_init(ntptime_shm_con *con)
{
  if (!con) return -20;
  ntptime_shm_deinit(con);
  if (shm_sbuf_init(&con->link, SHM_ADDR_NTPTIME, NTPTIME_SHMBUF_SIZE,
      sizeof(ntptime_shm_t))<0)
    return -21;
  if (shm_sbuf_init(&con->sync, SHM_ADDR_NTPTIME_SYNC, NTPTIME_SHMBUF_SIZE,
      sizeof(ntptime_t))<0)
      return -22;
  con->opened=1;

  return 0;
}

void ntptime_shm_deinit(ntptime_shm_con *con)
{
  if (!con) return;
  if (con->opened==1)
  {
    shm_sbuf_deinit(&con->link);
    shm_sbuf_deinit(&con->sync);
    con->opened=0;
  }
}

int ntptime_shm_getlastts(ntptime_shm_con *con,ntptime_t *dest)
{
  ntptime_shm_t tmp;

  if (!con || !dest) return -25;
  if (con->link.last(&con->link, &tmp) < 0)
    return -26;
  memcpy(dest, &tmp.ctime, sizeof(tmp.ctime));
  return 0;
}

double ntptime_shm_getlastd(ntptime_shm_con *con)
{
    ntptime_t tmp={0,0};

    if (ntptime_shm_getlastts(con, &tmp))
        return -26;
    return (double)tmp.sec + (double)tmp.psec*2.32830643654e-10;
}

int ntptime_shm_getlaststruct(ntptime_shm_con *con,ntptime_shm_t *dest)
{
    if (!con || !dest) return -25;
    if (con->link.last(&con->link, dest) < 0)
        return -27;
    return 0;
}

int ntptime_shm_getallstruct(ntptime_shm_con *con,ntptime_shm_t *dest)
{
  if (!con || !dest) return -28;
  if (con->link.linearize(&con->link, dest) < 0)
    return -30;
  return 0;
}

u64 ntptime_to_u64(const ntptime_t *t)
{
    return (((u64)t->sec) << 32) | (u64)t->psec;
}

ntptime_t ntptime_from_u64(u64 t)
{
    ntptime_t to_t;
    to_t.sec = (u32)(t >> 32);
    to_t.psec = (u32)(t & 0xffffffff);

    return to_t;
}
