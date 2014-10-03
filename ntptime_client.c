#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include <getopt.h>
#include <time.h>
#include <vu_tools/vu_tools.h>
#include <shmci/shmci.h>
#include <shmci/shmsbuf.h>
#include "ntptime.h"

extern char *optarg;

static const char *const PROGRAM_NAME     = "ntptime_client";
static const char *const PROGRAM_AUTHORS  = "";
static const char *const PROGRAM_DESC     = "";
static const char *const PROGRAM_INFO     = "";
static const char *const PROGRAM_LICENSE  = "";

static int REFRESH_TIME=1000000, conv=0, syncflag=0, deltas=0;
static ntptime_shm_con nsc;
static int ts_test;
static FILE *log=NULL;

static void parse_opts(int argc, char **argv)
{
  int c;
  while (1)
  {
    int option_index = 0;
    static struct option long_options[] = {
         {"help", no_argument, 0, 'h'},
         {"verbose", no_argument, 0, 'v'},
         {"refresh", required_argument,0, 0},
         {"ts-test", no_argument,0, 't'},
         {"sync", no_argument, 0, 0},
         {"deltas", no_argument, 0, 0},
         {0, 0, 0, 0}
     };

     c = getopt_long(argc, argv, "Hhvt",
                     long_options, &option_index);
     if (c == -1)
       break;

     switch (c) {
       case 0:
         if(!strcmp(long_options[option_index].name,"refresh"))
         {
           REFRESH_TIME=( atoi(optarg) > 0)? (atoi(optarg)) : REFRESH_TIME;
         }
         else if(!strcmp(long_options[option_index].name,"sync"))
         {
             syncflag=1;
         }
         else if(!strcmp(long_options[option_index].name,"deltas"))
         {
             deltas=1;
         }
         break;

       case 'H':
       case 'h':
         fprintf(stderr,"%s  %s\n", PROGRAM_NAME, PROGRAM_DESC );
         fprintf(stderr,"Version      %s rev.%s  (compiled %s)\n",
                   PROGRAM_VERSION, SVN_REVISION, __DATE__);
         fprintf(stderr,"License      %s\n",PROGRAM_LICENSE);
         fprintf(stderr,"Author(s)    %s\n\n",PROGRAM_AUTHORS);
         fprintf(stderr,
                "USAGE %s [OPTIONS]\n"
                "\n%s\n\n",
                PROGRAM_NAME,
                PROGRAM_INFO);
         fprintf(stderr, "\
Possible options:\n\
-h -H, --help  Print this help\n\n"
         );
         fprintf(stderr, "\
    --refresh  Set refresh rate in usec (def. %d)\n\n\
    --sync     Print only sync ts buffer contents\n",
         REFRESH_TIME);
         fprintf(stderr, "\
-v, --verbose  Convert time to convenient form and print more info\n\
-t, --ts-test  Write received time from shm and local ts to test\n\
               in binary file ./log.bin\n"
         );
         fprintf(stderr,"\
    --deltas   dump to stdout some info to test deltas:\n\
               current time | delta (all in floating point seconds)\n\
               you can plot it with python script\n"
         );
         exit(EXIT_SUCCESS);
         break;

       case 'v':
         conv=1;
         break;

       case 't':
         ts_test=1;
         break;

       /*  option not recognized  */
       case '?':
         break;

       default:
         fprintf(stderr,"[%s:%d] ?? getopt returned character code 0%o ??\n",
                 _FILE_,__LINE__,(unsigned int)c);
         exit(EXIT_FAILURE);
     } /*  of switch  */
   } /*  of while  */
   if (optind < argc) {
     fprintf(stderr,"[%s:%d] non-option ARGV-elements:",
             _FILE_,__LINE__);
     while (optind < argc)
         fprintf(stderr,"%s ", argv[optind++]);
     fprintf(stderr,"\n");
     exit(EXIT_FAILURE);
   }
} /*  of parse_opts  */

void sig_hand(int signum)
{
  (void)signum;
  if(ts_test)
    (void)fclose(log);
  ntptime_shm_deinit(&nsc);
  exit(EXIT_SUCCESS);
}

void Deltas(void)
{
int err;
double curt;
ntptime_shm_t oldst, newst;
memset(&oldst, 0, sizeof oldst);
memset(&newst, 0, sizeof newst);

if ((err = ntptime_shm_getlaststruct(&nsc, &oldst)) < 0)
{
    fprintf(stderr, "[%s:%d] Error #%d in ntptime_shm_getlaststruct():\n",
            _FILE_, __LINE__, err);
    abort();
}
while(1)
{
    usleep(REFRESH_TIME);
    if ((err = ntptime_shm_getlaststruct(&nsc, &newst)) < 0)
    {
        fprintf(stderr, "[%s:%d] Error #%d in ntptime_shm_getlaststruct():\n",
                _FILE_, __LINE__, err);
        abort();
    }
    if (!memcmp(&oldst, &newst, sizeof oldst))
        continue;
    memcpy(&oldst, &newst, sizeof oldst);
    curt = (double)newst.ctime.sec + (double)newst.ctime.psec*2.32830643654e-10;
    printf("%.10lf\t%.10lf\n", curt, newst.delta);
}
}

int main(int argc, char** argv)
{
  int idx=getpid(),err;
  ntptime_t res={0,0},localt;
  ntptime_shm_t rec;
  memset(&rec,0,sizeof(rec));

  /* read command line parameters */
  parse_opts( argc, argv );
  (void)signal(SIGTERM, sig_hand);
  (void)signal(SIGINT, sig_hand);
  (void)signal(SIGABRT, sig_hand);

  if(ts_test && !deltas)
    if( (log=fopen("./log_ntptimed.bin","wb"))==NULL )
    {
      fprintf(stderr,"Failed to open log file: ./log_ntptimed.bin\n");
      exit(EXIT_FAILURE);
    }
  if( (err=ntptime_shm_init(&nsc))<0 )
  {
    fprintf(stderr,"[%s:%d] #%d %s\nfrom shmsbuf: %s\n",_FILE_,__LINE__,
            err, shm_sbuf_error(err), nsc.link.err_msg);
    exit(EXIT_FAILURE);
  }

  if (deltas)
      Deltas();

  while(1)
  {
    usleep(REFRESH_TIME);
    if (!ts_test)
    {
      if (!syncflag)
      {
        if (conv==0)
        {
            if ((err=ntptime_shm_getlaststruct(&nsc,&rec)) < 0)
            fprintf(stderr,"[%s:%d] Error #%d in ntptime_shm_getlaststruct():\n",
                    _FILE_,__LINE__,err);
            fprintf(stderr,
                    "\nClient [%d]: timestamp received\n"
                    "cppb:\t%" PRIu64 "\n"
                    "time:\t%u.%u\n"
                    "delta:\t%f\n"
                    "====================================\n"
                    ,idx,
                    ((u64)rec.ctime.sec<<32)+(u64)rec.ctime.psec,
                    rec.ctime.sec,
                    (u32)((double)rec.ctime.psec/4.294967296),
                    rec.delta);
        }
        else
        {
            if ((err=ntptime_shm_getlaststruct(&nsc,&rec)) < 0)
            {
                fprintf(stderr,
                    "[%s:%d] Error in ntptime_shm_getlaststruct(): %s\nfrom shmsbuf: %s\n",
                    _FILE_,__LINE__,shm_sbuf_error(err),nsc.link.err_msg);
            }
            else
            {
                const time_t ttmp = rec.ctime.sec - NTP_UNIX_DELTA;
                fprintf(stderr,
                "\n\nClient [%d]: timestamp read\n"
                "Current time:      %s"
                "Current NTP time:  %.8f sec\n",
                idx,
                ctime(&ttmp),
                (double)rec.ctime.sec + (double)rec.ctime.psec/4294967296.
                );
                fprintf(stderr,
                "Time delta:        %f sec\n"
                "Sync status        %d\n"
                "Total ts received: %u\n"
                "Total crc errors:  %u\n",
                rec.delta,rec.sync_stat,rec.ts_count,rec.crcerr_count);
            }
        }
      }
      else
      {
          ntptime_t buf[NTPTIME_SHMBUF_SIZE];
          memset(buf, 0, sizeof(ntptime_t)*NTPTIME_SHMBUF_SIZE);
          if ((err = nsc.sync.linearize(&nsc.sync, buf)) < 0)
              fprintf(stderr,
                      "[%s:%d] Error: g_nsc.sync.linearize returned %d\n%s\n",
                      _FILE_,__LINE__,err,shm_sbuf_error(err));
          else
          {
            printf("\n--------------------------------\n");
            for (err=0; err<NTPTIME_SHMBUF_SIZE; ++err)
            {
                printf("#%d\t%9u.%09u\thex: %08X %08X\tcppb: %" PRIu64 "\n",
                       err, buf[err].sec, (u32)((double)buf[err].psec/4.294967296),
                       buf[err].sec, buf[err].psec,
                       ((u64)buf[err].sec<<32)+(u64)buf[err].psec
                      );
            }
          }
      }
    }
    else
    {
      if( (err=ntptime_shm_getlastts(&nsc,&res))<0 )
          fprintf(stderr,"[%s:%d] Error #%d in ntptime_shm_getts():\n",
                  _FILE_,__LINE__,err);
      localtime_get_gtd(&localt);
      fwrite(&res.sec,4,1,log);
      fwrite(&res.psec,4,1,log);
      fwrite(&localt.sec,4,1,log);
      fwrite(&localt.psec,4,1,log);
    }
  }

  return -1;
}
