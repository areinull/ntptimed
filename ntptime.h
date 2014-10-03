#ifndef __NTPTIME_H__
#define __NTPTIME_H__

#include <vu_tools/vu_tools.h>
#include <glink/glink.h>
#include <shmci/shmsbuf.h>
#include <shmci/shm_addr.h>

#ifdef __cplusplus
extern "C" {
#endif

/*  Error codes  */
#define NTPTIMEERR_NOFF     -1
#define NTPTIMEERR_TLOCAL   -2
#define NTPTIMEERR_CRC      -3
#define NTPTIMEERR_IO       -4

/* NTP timestamp format */
typedef struct
{
    u32 sec;  /*  seconds  */
    u32 psec; /*  "picoseconds"  */
} ntptime_t;

/* data structure used by ntptimed */
typedef struct {
  ntptime_t ctime;  /* timestamp */
  double delta;     /* delta = sync time - local time [sec] */
  u32 ts_count;     /* number of ts received from sync */
  u32 crcerr_count; /* number of crc errors in received ts */
  char sync_stat;   /* 0 - no sync, 1 - syncronizer is up */
} ntptime_shm_t;

/* ntptimed link context */
typedef struct {
  int opened;
  struct ShmSBuf link; /* ntptime_shm_t buffer with extrapolated timestamps */
  struct ShmSBuf sync; /* ntptime_t buffer with sync timestamps */
} ntptime_shm_con;

/* number of seconds between NTP epoch (1900) and Unix epoch (1970) */
static const u32 NTP_UNIX_DELTA = 2208988800u;  /* thanks to F.Lundh */

/*---------------------- ntptime low-level functions ----------------------- */

/*  Send ntp timestamp in syncro-packet
 *  Return value: 0 on success, error number < 0 otherwise */
int ntptime_send(GlinkContext *link,const ntptime_t *time);

/*  Get ntp timestamp from serial line
 *  Measure localtime at time of byte 0xff received, using function
 *  referenced by 4th argument.
 *  Return 0 on success, error number <0 on fail
 */
int ntptime_get(GlinkContext *link, ntptime_t *time, ntptime_t *localtime,
                int (*)(ntptime_t*));

/*-------------------- ntptime miscellaneous functions --------------------- */

/*  Get localtime in NTP format. Return 0 on success. It uses gettimeofday.
 */
int localtime_get_gtd(ntptime_t *localtime);

/*---------------------- ntptimed interface functions ---------------------- */

/*  Initialize shared memory buffer.
    Return 0 on success.
*/
#define NTPTIME_SHMBUF_SIZE 16 /* size of the buffer (in structures) */
int ntptime_shm_init(ntptime_shm_con *con);

/*  Break connection to shared memory and buffer */
void ntptime_shm_deinit(ntptime_shm_con *con);

/*  Get last ntp-timestamp from shared memory buffer.
    Return 0 on success.
*/
int ntptime_shm_getlastts(ntptime_shm_con *con,ntptime_t *dest);

/*  Get last ntp-timestamp from shared memory buffer and convert to double.
    Return current time in seconds.
 */
double ntptime_shm_getlastd(ntptime_shm_con *con);

/*  Get last ntptime_shm_t struct from shared memory buffer.
    Return 0 on success.
*/
int ntptime_shm_getlaststruct(ntptime_shm_con *con,ntptime_shm_t *dest);

/*  Get all ntptime_shm_t structs from shared memory buffer.
    Make sure, that 'dest' is big enough to store NTPTIME_SHMBUF_SIZE structs.
    Return 0 on success.
*/
int ntptime_shm_getallstruct(ntptime_shm_con *con,ntptime_shm_t *dest);

u64 ntptime_to_u64(const ntptime_t *t);

ntptime_t ntptime_from_u64(u64 from_t);

#ifdef __cplusplus
}
#endif

#endif /*  __NTPTIME_H__  */
