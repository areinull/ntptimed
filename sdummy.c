#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <glink/define.h>
#include <glink/glink.h>
#include "ntptime.h"

char *dev_name="/dev/ttyS0";


const int INT_SEC=1;  /*  timer interval [sec] */
const int INT_USEC=0; /*  timer interval [microsec]  */

time_t unix_timestamp;

GlinkHandle mylink;

void alarm_wakeup (int i)
{
    struct itimerval tout_val;
    u32 tmp;
    ntptime_t nt;

    /*  reset timer for next tick  */
    signal(SIGALRM,alarm_wakeup);

    tout_val.it_interval.tv_sec = 0;
    tout_val.it_interval.tv_usec = 0;
    tout_val.it_value.tv_sec  =  INT_SEC; 
    tout_val.it_value.tv_usec =  INT_USEC;

    setitimer(ITIMER_REAL, &tout_val,0);

    /*  get unix time  */
    unix_timestamp=time(NULL);

    /*  convert it to the ntp time  */
    nt.sec=unix_timestamp+NTP_UNIX_DELTA;

    /* FIXME fill fraction part later */ nt.psec=0;

    ntptime_send(mylink,&nt);

}

int main (int argc, char **argv) 
{


    /*  init serial line  */
    if( (mylink=glink_init_serial( dev_name, 115200 ) < 0 ) )
    {
        fprintf(stderr,"[%s:%d] Error: device '%s' open failed.\n"
                     "Err=%d\n",
                      _FILE_, __LINE__, dev_name, mylink);
        exit(EXIT_FAILURE);
    }
    
    char ch;
    while(1)
        {
        while( (ch=glink_get_byte(mylink))!=EOF)
       printf("%x\n",ch);
    }
    
    /*  setup timer first time */
    //alarm_wakeup(1);

    while(1)
        sleep(123);


    return EXIT_SUCCESS;
}
