#include "ntptime.h"
#include <inttypes.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    /* ntptimed shared memory context */
    ntptime_shm_con context;
    /* shared memory data structure */
    ntptime_shm_t data;
    /* stores function return code */
    int err = 0;
    /* number of seconds from Unix epoch */
    time_t cur_time;
    memset(&context, 0, sizeof context);
    memset(&data, 0, sizeof data);

    /* initialize ntptimed shm context */
    err = ntptime_shm_init(&context);
    if (err < 0)
    {
        fprintf(stderr, "[%s:%d] ntptime context init failed\n"
                        "ntptime_shm_init() returned %d: %s\n"
                        "shmsbuf error message: %s\n",
                __FILE__, __LINE__, err, shm_sbuf_error(err),
                context.link.err_msg);
        exit(EXIT_FAILURE);
    }

    /* get most recent data structure from shm */
    err = ntptime_shm_getlaststruct(&context, &data);
    if (err < 0)
    {
        fprintf(stderr, "[%s:%d] failed to receive last data structure\n"
                        "ntptime_shm_getlaststruct() returned %d: %s\n"
                        "shmsbuf error message: %s\n",
                __FILE__, __LINE__, err, shm_sbuf_error(err),
                context.link.err_msg);
        ntptime_shm_deinit(&context);
        exit(EXIT_FAILURE);
    }

    /* convert NTP time to Unix epoch */
    cur_time = data.ctime.sec - NTP_UNIX_DELTA;

    /* print status and current system time in different formats */
    printf("\
current time (string):    %s\
current time    (u64):    %"PRIu64"\n\
current time (double):    %.8f sec\n\
time delta:               %.8f sec\n\
sync status:              %s\n\
sync packets received:    %u\n\
total crc errors:         %u\n",
           ctime(&cur_time),
           (uint64_t)data.ctime.sec << 32 | (uint64_t)data.ctime.psec,
           (double)data.ctime.sec + (double)data.ctime.psec/4294967296.,
           data.delta,
           data.sync_stat? "connected": "disconnected",
           data.ts_count,
           data.crcerr_count);

    /* free aquired resources */
    ntptime_shm_deinit(&context);
    exit(EXIT_SUCCESS);
}
