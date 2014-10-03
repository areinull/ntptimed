#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <vu_tools/vu_tools.h>
#include <glink/glink.h>
#include "ntptime.h"

#define SEND_TIME_INTERVAL 500000  /* microseconds */

GlinkContext mylink;

static void signal_handler(int nsig)
{
  (void)nsig;
  glink_flush(&mylink);
  if( glink_close(&mylink)<0 )
  {
    fprintf(stderr,"Error closing files\n");
    exit(EXIT_FAILURE);
  }
  exit(EXIT_SUCCESS);
}

int main (void)
{
    ntptime_t nt,nt0;
    double a,b;
    (void)signal(SIGINT, signal_handler);
    (void)signal(SIGTERM, signal_handler);
//     glink_init_file(&mylink,"/tmp/test1.temp","/tmp/test2.temp");
    glink_init_udp(&mylink,"localhost",63001,63002);
    if(!mylink.opened)
    {
            fprintf(stderr,"[%s:%d] can't open link: \n %s\n ",
                    _FILE_, __LINE__,glink_strerror(&mylink));
            glink_close(&mylink);
            exit(EXIT_FAILURE);
    }
    glink_io_block(&mylink,0);
    glink_set_timeout(&mylink,1.0);

    localtime_get_gtd(&nt0);
    a=nt0.psec/4294967296.0+nt0.sec;

    while(1)
    {
      usleep(SEND_TIME_INTERVAL);
      localtime_get_gtd(&nt);
      b=nt.psec/4294967296.0+nt.sec-a;
      nt.sec=(u32)b;
      nt.psec=(u32)((b-nt.sec)*4294967296.0);
      ntptime_send(&mylink,&nt);
//       glink_flush(&mylink);
    }
    return -1;
}
