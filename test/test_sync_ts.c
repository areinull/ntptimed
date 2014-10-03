#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <vu_tools/vu_tools.h>
#include <glink/glink.h>
#include "ntptime.h"

static const char *const PROGRAM_NAME     = "test_sync_ts";
static const char *const PROGRAM_AUTHORS  = "";
static const char *const PROGRAM_DESC     = "";
static const char *const PROGRAM_INFO     = "";
static const char *const PROGRAM_LICENSE  = "GPL v2";

static GlinkContext glink;
static FILE *log=NULL;
static char device[FILENAME_MAX]="/dev/ttyM1",
            logname[FILENAME_MAX];
static int speed=115200;
static double timeout=3.0;

static void parse_opts(int argc, char **argv)
{
  int c;
  while (1) 
  {
    int option_index = 0;
    static struct option long_options[] = {
         {"help", no_argument, 0, 'h'},
         {"dev", required_argument, 0, 'd'},
         {"speed", required_argument, 0, 's'},
         {"logname", required_argument, 0, 'l'},
         {"wait-time", required_argument, 0, 'w'},
         {0, 0, 0, 0}
     };

     c = getopt_long(argc, argv, "Hhd:s:l:w:",
                     long_options, &option_index);
     if (c == -1)
       break;

     switch (c) {
       case 0:
         break;

       case 'H': case 'h':
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
         fprintf(stderr,
         "Possible options:\n"
         "-h -H, --help  Print this help\n\n" 
         );
         fprintf(stderr,
         "-d, --dev      Device to use (%s)\n"
         "-s, --speed    Speed of device (%d)\n",
         device,speed);
         fprintf(stderr,
         "-l, --logname   Specify log file name, stdout if ommited (%s)\n"
         "-w              Define timeout (%f sec)\n",
         logname,timeout);
         exit(EXIT_SUCCESS);
         break;

       case 'd':
         if(strlen(optarg) >= FILENAME_MAX)
         {
           fprintf(stderr,"[%s:%d] Error: device name too long\n",
                   _FILE_,__LINE__);
           exit(EXIT_FAILURE);
         }
         strcpy(device,optarg);
         break;

       case 's':
         speed = ( atoi(optarg) > 0)? (atoi(optarg)) : speed;
         break;

       case 'l':
         if(strlen(optarg) >= FILENAME_MAX)
         {
           fprintf(stderr,"[%s:%d] Error: logfile name too long\n",
                   _FILE_,__LINE__);
           exit(EXIT_FAILURE);
         }
         strcpy(logname,optarg);
         break;

       case 'w':
           timeout = ( strlen(optarg) > 0)? (atof(optarg)) : timeout;
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

static void signal_handler(int nsig)
{
  (void)nsig;
  if( log!=NULL )
    (void)fclose(log);
  if( glink.opened !=0 )
    (void)glink_close(&glink);
  exit(EXIT_SUCCESS);
}

static int receive(void)
{
  ntptime_t localt;
  char buf[18];
  int err;

  if(glink_init_serial( &glink, device, speed )!=0)
  {
    (void)fprintf(stderr,
                  "[%s:%d] Device '%s' open failed.\n%s\n",
                  _FILE_, __LINE__,device,glink_strerror(&glink));
    (void)glink_close(&glink);
    return -1;
  }
  glink_io_block(&glink, 1);
  glink_set_timeout(&glink, timeout);

  while(1)
  {
    if ((err = glink_get_buf(&glink,buf,10)) < 0)
    {
      (void)fprintf(stderr,
                    "Warning: glink_get_buf() failed with error #%d: %s\n",
                    err,glink_strerror(&glink));
    }
    localtime_get_gtd(&localt);

    if( strlen(logname)!=0 )
    {
      fwrite(buf,10,1,log);
      fwrite(&localt.sec,4,1,log);
      fwrite(&localt.psec,4,1,log);
    }
    else
    {
      memcpy(&buf[10],&localt.sec,4);
      memcpy(&buf[14],&localt.psec,4);
      hexdump(buf,18,stdout);
    }
  }
  
  return -99;
}

int main(int argc, char **argv)
{
  /* read command line parameters */
  parse_opts( argc, argv );
  (void)signal(SIGINT, signal_handler);
  (void)signal(SIGTERM, signal_handler);
  
  if( strlen(logname)!=0 )
  {
    if( (log=fopen(logname,"wb"))==NULL )
    {
      fprintf(stderr,"Failed to open log file: %s\n",logname);
      exit(EXIT_FAILURE);
    }
  }
  return receive();
}
