#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <iniparser/iniparser.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <vu_tools/vu_tools.h>
#include <glink/glink.h>
#include <shmci/shmci.h>
#include <shmci/shmsbuf.h>
#include <shmci/shm_addr.h>
#include <prx_mysql/prx_mysql.h>
#include <syslog.h>
#include "ntptime.h"

extern char *optarg;

static const char *const PROGRAM_NAME     = "";
static const char *const PROGRAM_AUTHORS  = "";
static const char *const PROGRAM_DESC     = "";
static const char *const PROGRAM_INFO     = "";
static const char *const PROGRAM_LICENSE  = "";

/* the path to lock file */
static const char *const g_LockFilePath = "/var/run/ntptimed.pid";
/* the file descriptor of the lock file */
static int g_LockFileDesc=-1;

static GlinkContext g_glink; /* link to serial line */

/* link options */
static char g_line[255];
static int g_isLine = 0;

static prx_mysql_context prx_param;
static ntptime_shm_con g_nsc; /* ntptime context */

static enum {DELTA_BUF_SIZE=16};
static unsigned int g_diter=0;                   /* delta buffer 'iterator' */
static double    g_delta_buf[DELTA_BUF_SIZE];    /* stores time deltas */
static unsigned int g_dditer=0;                  /* ddelta buffer index */
static double    g_ddelta_buf[DELTA_BUF_SIZE-1]; /* stores delta differential */
static double    g_delta_fil;           /* filtered delta */
static double    g_ddelta_fil;          /* filtered delta differential */
static double    g_last_tsld;           /* last local time of sync ts receive */
static unsigned int g_deltas_written;
static double       g_dfilter_norm, g_ddfilter_norm;
static struct /* filter's coefficients */
{
    unsigned int total; /* quantity of coefficients */
    double a[6];
} koef =
{
    6,
    {6., 5., 4., 3., 2., 1.}
};
static unsigned int g_ts_count,      /* received timestamps counter */
                    g_crcerr_count;  /* CRC errors counter */
static char   g_sync_stat;           /* if syncronizer works */
static double g_extrap_time;  /* extrapolation time interval */
static double g_syncTO;       /* syncronization timeout */
static double g_without_sync; /* working time without syncronizer */
static double g_prx_upd;      /* prx db update period in sec */

static pthread_t g_thread=0;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static const struct timespec g_mutex_to = {1,0};

static int g_useStartTime = 0;
static ntptime_t g_startTime = {0,0};
static double syslog_warnTO;  /* "sync is down" msg warning period in sec */
static enum work_mode_e
{
    WM_DEFAULT,
    WM_ALONE
} work_mode = WM_DEFAULT;
static char g_nodaemon = 0;   /* do not fork into background */
static char g_useDb = 1;      /* use mysql db (via prx_mysql) */

static volatile sig_atomic_t g_caught_hup_signal=0;

/* using this function for localtime calculation */
static int (*const localtime_func)(ntptime_t*) = &localtime_get_gtd;

/*'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''*/
/*  Prototypes  */

static void quit(int exitval);
/* read and proccess command line options and parameters */
static void parse_opts(int argc, char **argv);
/* parse cfg file and open connection to the line (or files)
   pcfgoc = Parse CFG and Open Connection */
static void pcfgoc(void);
static int BecomeDaemonProcess(
           const char *const lockFileName,      /* the path to the lock file */
           int *const lockFileDesc,  /* the file descriptor of the lock file */
           pid_t *const thisPID      /* the PID of this process after fork()
                                             has placed it in the background */
           );
inline static void ConfigureSignalHandlers(void);
static void FatalSigHandler(int sig);
static void HupSigHandler(int sig);
static void HupSigRoutine(void);
static int CreateThread(pthread_t *thread,void*(*routine)(void*));
static void* time_extrap(void* arg);
static void extrap(void);
static void dFilter(void);             /* update g_delta_fil and g_ddelta_fil */
static void prx_mysql_set_param_ext(prx_mysql_context *cont, const char *name, const char *val);

/*'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''*/
int main(int argc, char **argv)
{
  int result, abrupt_change=0;
  pid_t daemonPID;
  ntptime_t tsi={0,0}, tsl={0,0};
  double tsid=0., prevtsid=0., tsld=0., start=0.;
  double syslog_warn_time=0.;

  /* read command line parameters */
  parse_opts(argc, argv);

  /* become a daemon */
  if (!g_nodaemon)
  {
    if ((result = BecomeDaemonProcess(g_LockFilePath, &g_LockFileDesc,
                                      &daemonPID)) < 0)
    {
        fprintf(stderr,
                "[%s:%d] Failed to become daemon process.\n"
                "Returned error number %d\n",
                _FILE_, __LINE__, result);
        quit(EXIT_FAILURE);
    }
  }

  /* set up signal processing */
  ConfigureSignalHandlers();

  /* init prx */
  if (g_useDb)
  {
    result = prx_mysql_init(&prx_param, "ntptimed", "Ntptimed");
    if (result < 0)
    {
        fprintf(stderr, "[%s:%d] Prx initialize failed with #%d:\n%s\n",
                _FILE_, __LINE__, result, prx_param.err_msg);
        quit(EXIT_FAILURE);
    }
    result = prx_mysql_mod_add(&prx_param, "ntptimed", "Ntptimed");
    if (result < 0)
    {
        fprintf(stderr, "[%s:%d] Ntptimed prx_mysql_mod_add failed with #%d:\n%s\n",
                _FILE_, __LINE__, result, prx_param.err_msg);
        quit(EXIT_FAILURE);
    }
    else
    {
        const char *err_msg = "Ntptimed prx module started";
#ifdef NTPTIMED_DEBUG
        fprintf(stderr, "%s\n", err_msg);
#endif
        prx_mysql_log(&prx_param, _FILE_, __LINE__, E_NOTICE, err_msg);
    }
    /* check whether module already exists */
    result = prx_mysql_mod_exist(&prx_param, "ntptimed");
    if (result > 0)
    {   /* result>0 if other similar modules exist */
        fprintf(stderr, "Warning: %d 'ntptimed' module(s) already exist\n",
                result);
    }
    prx_mysql_log(&prx_param, _FILE_, __LINE__, E_NOTICE,
            "Ntptimed connected to mysql (prx link)");
#ifdef NTPTIMED_DEBUG
    fprintf(stderr, "Ntptimed connected to mysql (prx link)\n");
#endif
  }
  else
  {
      openlog("ntptimed", LOG_PID, LOG_USER);
      syslog(LOG_NOTICE, "Ntptimed is run without prx_mysql support");
#ifdef NTPTIMED_DEBUG
      fprintf(stderr, "Ntptimed is run without prx_mysql support\n");
#endif
  }
#ifdef NTPTIMED_DEBUG
  fprintf(stderr, "Work mode is %s\n", (work_mode==WM_ALONE)?"ALONE":"DEFAULT");
#endif

  if (work_mode == WM_ALONE)
  {
    if (g_useDb)
        prx_mysql_log(&prx_param, _FILE_, __LINE__, E_NOTICE, "Work mode is ALONE");
    else
        syslog(LOG_NOTICE, "Work mode is ALONE");
    prx_mysql_set_param_ext(&prx_param, "Work mode", "Alone");
  }
  else
  {
    if (g_useDb)
        prx_mysql_log(&prx_param, _FILE_, __LINE__, E_NOTICE, "Work mode is DEFAULT");
    else
        syslog(LOG_NOTICE, "Work mode is DEFAULT");
    prx_mysql_set_param_ext(&prx_param, "Work mode", "Default");
  }

  pcfgoc();

  for (result = 0; result < (int)koef.total; ++result)
  {
      g_dfilter_norm += koef.a[result];
  }
  g_ddfilter_norm = g_dfilter_norm - koef.a[koef.total-1];

  if (g_useStartTime)
  {
      localtime_func(&tsl);
      tsld = (double)tsl.sec + (double)tsl.psec/4294967296.;
      tsid = (double)g_startTime.sec + (double)g_startTime.psec/4294967296.;
      g_delta_fil = g_delta_buf[0] = tsid - tsld;
      ++g_deltas_written;
  }

  /* starting thread */
  if ((result = CreateThread(&g_thread, &time_extrap)) < 0)
  {
    fprintf(stderr,
            "[%s:%d] Failed to start new thread.\n"
            "Returned error number %d\n",
            _FILE_, __LINE__, result);
    if (g_useDb)
        prx_mysql_log(&prx_param, _FILE_, __LINE__, E_FATAL, "CreateThread() failed");
    else
        syslog(LOG_ERR, "[%s:%d] CreateThread() failed", _FILE_, __LINE__);
    quit(EXIT_FAILURE);
  }

  /* do required job */
  while(1)
  {
    if (!g_caught_hup_signal)
    {
      if (work_mode == WM_ALONE)
      {
        pause();
        continue;
      }
      /* do if we got incoming timestamp from syncronizer */
      if ((result = ntptime_get(&g_glink, &tsi, &tsl, localtime_func)) == 0)
      {
        if ((result = g_nsc.sync.add(&g_nsc.sync, &tsi, 1)) < 0)
            fprintf(stderr, "[%s:%d] Error: g_nsc.sync.add returned %d\n%s\n",
                    _FILE_, __LINE__, result, shm_sbuf_error(result)
                   );
        prevtsid = tsid;
        tsid = (double)tsi.sec + (double)tsi.psec/4294967296.;
        tsld = (double)tsl.sec + (double)tsl.psec/4294967296.;
#ifdef NTPTIMED_DEBUG
        fprintf(stderr,
                "\n====================================\n"
                "Daemon: timestamp received\n"
                "time:\t%f sec\nlocal:\t%f sec\n"
                "Total ts received:  %u\n"
                "Total crc errors:   %u\n",
                tsid, tsld, g_ts_count, g_crcerr_count);
#endif
        if (g_deltas_written && (fabs(tsid - prevtsid - g_without_sync) > g_syncTO))
        {
            char tmp[128];
            abrupt_change = 1;
            snprintf(tmp, 128, "Syncronizer's time abrupt change: %f sec",
                     tsid - prevtsid - g_without_sync);
            if (g_useDb)
                prx_mysql_log(&prx_param, _FILE_, __LINE__, E_NOTICE, tmp);
            else
                syslog(LOG_NOTICE, "%s", tmp);
#ifdef NTPTIMED_DEBUG
            fprintf(stderr, "%s\n", tmp);
#endif
        }
        pthread_mutex_lock(&g_mutex);
        if (abrupt_change)
        {
            g_deltas_written = 0;
            abrupt_change = 0;
        }
        else
        {
            g_diter = (g_diter+1)%DELTA_BUF_SIZE;
            g_dditer = (g_dditer+1)%(DELTA_BUF_SIZE-1);
        }
        g_delta_buf[g_diter] = tsid - tsld;
        ++g_deltas_written;
        if (g_deltas_written > 1)
        {
            g_ddelta_buf[g_dditer] =
(g_delta_buf[g_diter] - g_delta_buf[(g_diter+(DELTA_BUF_SIZE-1))%DELTA_BUF_SIZE])/
(tsld - g_last_tsld);
        }
        g_last_tsld = tsld;
        dFilter();
        ++g_ts_count;
        g_without_sync = 0.;
        if (g_sync_stat == 0)
        {
          g_sync_stat = 1;
          if (g_useDb)
            prx_mysql_log(&prx_param, _FILE_, __LINE__, E_NOTICE,
                    "Syncronizer is up now");
          else
            syslog(LOG_NOTICE, "Syncronizer is up now");
        }
        pthread_mutex_unlock(&g_mutex);
        start = 0.;
        continue;
      }
      else if (result == NTPTIMEERR_CRC)
      {
        pthread_mutex_lock(&g_mutex);
        ++g_ts_count;
        ++g_crcerr_count;
        pthread_mutex_unlock(&g_mutex);
        continue;
      }
      if (start == 0.) start = dtime();
      g_without_sync = dtime() - start;
      if (g_without_sync>g_syncTO)
      {
        pthread_mutex_lock(&g_mutex);
        g_sync_stat = 0;
        pthread_mutex_unlock(&g_mutex);
        if ((dtime() - syslog_warn_time) > syslog_warnTO)
        {
          char err_msg[0x100];
          snprintf(err_msg, 0x100,
                   "Syncronizer is down for %f sec", g_without_sync);
#ifdef NTPTIMED_DEBUG
          fprintf(stderr, "%s\n", err_msg);
#endif
          if (g_useDb)
            prx_mysql_log(&prx_param, _FILE_, __LINE__, E_NOTICE, err_msg);
          else
            syslog(LOG_NOTICE, "%s", err_msg);
          syslog_warn_time = dtime();
        }
      }
    }
    else /* SIGHUP received */
    {
      HupSigRoutine();
    }
  }
  /* shouldn't be here */
  if (g_useDb)
    prx_mysql_log(&prx_param, _FILE_, __LINE__, E_FATAL,"Reached inaccessible point");
  else
    syslog(LOG_ERR, "Reached inaccessible point");
  fprintf(stderr, "[%s:%d] Shouldn't be here! (end)\n", _FILE_, __LINE__);
  quit(EXIT_FAILURE);
}

/*'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''*/
static void quit(int exitval)
{
#ifdef NTPTIMED_DEBUG
  fprintf(stderr, "\
Total ts received:  %u\n\
Total crc errors:   %u\n",
          g_ts_count, g_crcerr_count);
#endif
  if (g_thread != 0)
  {
    if (pthread_cancel(g_thread))
    {
      if (errno == ESRCH)
          fprintf(stderr, "[%s:%d] No thread with that ID"
                          "could be found\n", _FILE_, __LINE__);
      else
          fprintf(stderr, "[%s:%d] Unexpected error\n", _FILE_, __LINE__);
    }
    if (pthread_join(g_thread, NULL))
    {
        fprintf(stderr, "[%s:%d] Error: %s\n",
                _FILE_, __LINE__, strerror(errno));
    }
#ifdef NTPTIMED_DEBUG
    fprintf(stderr, "[%s:%d] Done: exiting thread\n", _FILE_, __LINE__);
#endif
  }
  if (g_glink.opened != 0)
  {
    if (glink_close(&g_glink) < 0)
    {
#ifdef NTPTIMED_DEBUG
      fprintf(stderr, "[%s:%d] Error: closing connection\n", _FILE_, __LINE__);
#endif
    }
#ifdef NTPTIMED_DEBUG
    else
      fprintf(stderr, "[%s:%d] Done: closing connection\n", _FILE_, __LINE__);
#endif
  }
  ntptime_shm_deinit(&g_nsc);
  if (g_useDb)
  {
    if (prx_param.prog_active || prx_param.mod_active)
    {
        prx_mysql_log(&prx_param, _FILE_, __LINE__, E_NOTICE,
                "Ntptimed is now shutting down");
        prx_mysql_set_param_ext(&prx_param, "Enabled", "0");
        prx_mysql_mod_close(&prx_param);
        prx_mysql_close(&prx_param);
    }
  }
  else
  {
    syslog(LOG_NOTICE, "Ntptimed is now shutting down");
    closelog();
  }
  if (g_LockFileDesc != -1)
  {
    (void)close(g_LockFileDesc);
    (void)unlink(g_LockFilePath);
    g_LockFileDesc = -1;
#ifdef NTPTIMED_DEBUG
    fprintf(stderr, "[%s:%d] Done: closing and deleting lockfile\n",
            _FILE_, __LINE__);
#endif
  }

  exit(exitval);
}

/*'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''*/
static void parse_opts(int argc, char **argv)
{
  int c;
  while (1)
  {
    int option_index = 0;
    static struct option long_options[] = {
          {"help",      no_argument,       0, 'h'},
          {"transport", required_argument, 0, 't'},
          {"mysql",     required_argument, 0, 'm'},
          {"line",      required_argument, 0,  0 },
          {"alone",     no_argument,       0,  0 },
          {"no-daemon", no_argument,       0,  0 },
          {0, 0, 0, 0}
      };

    c = getopt_long(argc, argv, "Hhc:r:m:s:p:", long_options, &option_index);
    if (c == -1)
      break;

    switch (c)
    {
      case 0:
        /*  long options parsing  */
        if (!strcmp(long_options[option_index].name, "line"))
        {
            strcpy(g_line, optarg);
            g_isLine = 1;
        }

        if (!strcmp(long_options[option_index].name, "alone"))
        {
          work_mode = WM_ALONE;
        }

        if (!strcmp(long_options[option_index].name, "no-daemon"))
        {
          g_nodaemon = 1;
        }
        break;

      case 'H': case 'h':
        printf("%s     %s\n", PROGRAM_NAME, PROGRAM_DESC );
        printf("Version      %s rev.%s  (compiled %s)\n",
               PROGRAM_VERSION, SVN_REVISION, __DATE__);
        printf("License      %s\n", PROGRAM_LICENSE);
        printf("Author(s)    %s\n\n", PROGRAM_AUTHORS);
        printf("USAGE %s [OPTIONS]\n%s\n", PROGRAM_NAME, PROGRAM_INFO);
        puts("\
Possible options:\n\
-h -H  --help               Print this help\n\
-c  [stop|reconf]           Execute command:\n\
     stop    -  kill the daemon if it's running\n\
     reconf  -  reread configuration file\n\
-r                          Remove pid file\n\
-m  --mysql   true|false    Use MYSQL (default: true)\n\
--alone                     Work without sync\n\
--no-daemon                 Do not fork into background\n\
\n\
Compile with -DNTPTIMED_DEBUG to see debugging messagess\n\
When debug mode is used, it's HIGHLY recommended\n\
to redirect standart error to some file (e.g. \"2>log\")\n\
\n\
Start time in NTP (without sync):\n\
    -s          seconds\n\
    -p          fraction\n"
           );
        fprintf(stderr,
"--line                      line options see\n"
"                            Glink options section below\n"
"%s\n"
           , glink_help()
           );
        exit(EXIT_SUCCESS);

      case 'r':
        if (unlink(g_LockFilePath) < 0)
        {
          perror("Can't remove pid file");
          quit(EXIT_FAILURE);
        }
        else quit(EXIT_SUCCESS);

      case 'c':
      {
        int fd,len;
        pid_t pid;
        char pid_buf[16];

        if ((fd = open(g_LockFilePath, O_RDONLY)) < 0)
        {
          perror("Lock file not found. May be the daemon is not running?");
          quit(EXIT_FAILURE);
        }
        len = read(fd, pid_buf, 16);
        pid_buf[len] = 0;
        pid = atoi(pid_buf);
        if (!strcmp(optarg, "stop"))
        {
          kill(pid, SIGTERM);
          quit(EXIT_SUCCESS);
        }
        if (!strcmp(optarg, "reconf"))
        {
          kill(pid, SIGHUP);
          quit(EXIT_SUCCESS);
        }
        printf("Usage %s -c [stop|reconf]\n", argv[0]);
        quit(EXIT_FAILURE);
      }

      case 's':
        g_useStartTime = 1;
        g_startTime.sec = (u32)strtoul(optarg, NULL, 0);
        break;

      case 'p':
        g_useStartTime = 1;
        g_startTime.psec = (u32)strtoul(optarg, NULL, 0);
        break;

      case 'm':
        str_to_lower(optarg);
        if (!strcmp(optarg, "true"))
        {
            g_useDb = 1;
            break;
        }
        if (!strcmp(optarg, "false"))
        {
            g_useDb = 0;
            break;
        }
        printf("[%s:%d] Warning: illegal argument '%s', using default value\n",
               _FILE_, __LINE__, optarg);
        g_useDb = 1;
        break;

      /*  option not recognized  */
      case '?':
        break;

      default:
        printf("[%s:%d] ?? getopt returned character code 0%o ??\n",
               _FILE_, __LINE__, (unsigned int)c);
        exit(EXIT_FAILURE);
     } /*  of switch  */
   } /*  of while  */
   if (optind < argc)
   {
     printf("[%s:%d] non-option ARGV-elements:", _FILE_, __LINE__);
     while (optind < argc)
         printf("%s ", argv[optind++]);
     puts("\n");
     quit(EXIT_FAILURE);
   }
} /*  of parse_opts  */

/*'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''*/
static int BecomeDaemonProcess(const char *const lockFileName,
                               int *const lockFileDesc,
                               pid_t *const thisPID)
{
  int curPID, lockResult, killResult, lockFD, i, numFiles;
  char pidBuf[17], *lfs, pidStr[7];
  FILE *lfp;
  unsigned long lockPID;
  struct flock exclusiveLock;

  chdir("/");

  /* try to grab the lock file */
  lockFD = open(lockFileName, O_RDWR|O_CREAT|O_EXCL, 0644);

  if (lockFD == -1)
  {
    /* Perhaps the lock file already exists. Try to open it */
    lfp = fopen(lockFileName, "r");

    if (lfp == 0)
    {
      perror("Can't get lockfile");
      return -1;
    }

    /* Lockfile is opened. Find out the PID. */
    lfs = fgets(pidBuf, 16, lfp);

    if (lfs != 0)
    {
      if (pidBuf[strlen(pidBuf)-1] == '\n') /* strip linefeed */
      pidBuf[strlen(pidBuf)-1] = 0;
      lockPID = strtoul(pidBuf, (char**)0, 10);

      /* see if that process is running. Signal 0 in kill(2) doesn't
         send a signal, but still performs error checking */
      killResult = kill(lockPID, 0);
      if (killResult == 0)
      {
        fprintf(stderr,
                "\nERROR\n"
                "A lock file %s has been detected. It appears it is owned\n"
                "by the (active) process with PID %ld.\n",
                lockFileName, lockPID);
      }
      else if (errno == ESRCH) /* non-existent process */
      {
        fprintf(stderr,
                "\nERROR\n"
                "A lock file %s has been detected. It appears it is owned\n"
                "by the process with PID %ld, which is now defunct.\n"
                "Delete the lock file and try again.\n",
                lockFileName, lockPID);
      }
      else
      {
        perror("Could not acquire exclusive lock on lock file");
      }
    }
    else
      perror("Could not read lock file");

    fclose(lfp);
    return -2;
  }

  /* Set a lock on lock file. */
  exclusiveLock.l_type = F_WRLCK; /* exclusive write lock */
  exclusiveLock.l_whence = SEEK_SET; /* use start and len */
  exclusiveLock.l_len = exclusiveLock.l_start = 0; /* whole file */
  exclusiveLock.l_pid = 0;
  lockResult = fcntl(lockFD, F_SETLK, &exclusiveLock);

  if (lockResult < 0) /* can't get a lock */
  {
    close(lockFD);
    perror("Can't get lockfile");
    return -3;
  }

  /* Now move into the background and become a daemon. */
  curPID = fork();

  switch (curPID)
  {
    case 0: /* child process */
      break;

    case -1: /* error */
      perror("Initial fork failed");
      return -4;
      break;

    default: /* parent process, so exit */
      exit(EXIT_SUCCESS);
  }

  /* Make the process a session and process group leader. */
  if (setsid() < 0)
  {
    perror("setsid() failed");
    return -5;
  }

  /* Log PID to lock file. */
  /* Truncate just in case file already existed. */
  if (ftruncate(lockFD, 0) < 0)
  {
    perror("ftruncate() failed");
    return -6;
  }

  /* Store the PID. */
  *thisPID = getpid();
  sprintf(pidStr, "%d\n", (int)*thisPID);
  write(lockFD, pidStr, strlen(pidStr));

  *lockFileDesc = lockFD; /* return lock file descriptor to caller */

  /* Close open file descriptors. */
  numFiles = sysconf(_SC_OPEN_MAX); /* how many file descriptors? */
  for (i = numFiles-1; i > 2; --i) /* close open files except lock */
  {
    if (i != lockFD) /* don't close the lock file! */
      (void)close(i);
  }

  /* stdin/out to /dev/null, leave stderr as is */
  freopen("/dev/null", "r", stdin);
  freopen("/dev/null", "w", stdout);

  /* Put server into its own process group. */
  setpgrp();

  return 0;
}

/*'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''*/
inline static void ConfigureSignalHandlers(void)
{
  signal(SIGUSR1, SIG_IGN);
  signal(SIGUSR2, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGALRM, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
  signal(SIGURG, SIG_IGN);
  signal(SIGXCPU, SIG_IGN);
  signal(SIGXFSZ, SIG_IGN);
  signal(SIGVTALRM, SIG_IGN);
  signal(SIGPROF, SIG_IGN);
  signal(SIGIO, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);

  signal(SIGQUIT, FatalSigHandler);
  signal(SIGILL, FatalSigHandler);
  signal(SIGTRAP, FatalSigHandler);
  signal(SIGABRT, FatalSigHandler);
  signal(SIGIOT, FatalSigHandler);
  signal(SIGBUS, FatalSigHandler);
  signal(SIGFPE, FatalSigHandler);
  signal(SIGSEGV, FatalSigHandler);
  signal(SIGSTKFLT, FatalSigHandler);
  signal(SIGCONT, FatalSigHandler);
  signal(SIGPWR, FatalSigHandler);
  signal(SIGSYS, FatalSigHandler);
  signal(SIGTERM, FatalSigHandler);
  signal(SIGINT, FatalSigHandler);

  signal(SIGHUP, HupSigHandler);

  return;
}

/*'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''*/
static void FatalSigHandler(int sig)
{
  char err_msg[0x100];
#ifdef _GNU_SOURCE
  snprintf(err_msg, 0x100, "Caught signal: %s - exiting", strsignal(sig));
  if (g_useDb)
    prx_mysql_log(&prx_param, _FILE_, __LINE__, E_NOTICE, err_msg);
  else
    syslog(LOG_NOTICE, "%s", err_msg);
#else
  snprintf(err_msg, 0x100, "Caught signal: %d - exiting", sig);
  if (g_useDb)
    prx_mysql_log(&prx_param, _FILE_, __LINE__, E_NOTICE, err_msg);
  else
    syslog(LOG_NOTICE, "%s", err_msg);
#endif
  quit(EXIT_SUCCESS);
}

/*'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''*/
static void HupSigHandler(int sig)
{
  (void)sig;
  signal(SIGHUP, SIG_IGN);
  if (g_useDb)
    prx_mysql_log(&prx_param, _FILE_, __LINE__, E_NOTICE,
            "Caught HUP signal: reconfiguring...");
  else
    syslog(LOG_NOTICE, "Caught HUP signal: reconfiguring...");
#ifdef NTPTIMED_DEBUG
  fprintf(stderr, "Caught HUP signal: reconfiguring...\n");
#endif
  g_caught_hup_signal=1;
}

/*'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''*/
static void HupSigRoutine(void)
{
  int result;

#ifdef NTPTIMED_DEBUG
  fprintf(stderr, "\n"
          "Total ts received:  %u\n"
          "Total crc errors:   %u\n",
          g_ts_count, g_crcerr_count);
#endif
  if (g_thread != 0)
  {
    if (pthread_cancel(g_thread))
    {
      if (errno == ESRCH)
          fprintf(stderr, "[%s:%d] No thread with that ID"
                          "could be found\n", _FILE_, __LINE__);
      else
          fprintf(stderr, "[%s:%d] Unexpected error\n", _FILE_, __LINE__);
      quit(EXIT_FAILURE);
    }
#ifdef NTPTIMED_DEBUG
    fprintf(stderr, "[%s:%d] Done: exiting thread\n", _FILE_, __LINE__);
#endif
    if (g_useDb)
        prx_mysql_log(&prx_param, _FILE_, __LINE__, E_NOTICE,
                "Stopping extrapolation thread");
    else
        syslog(LOG_NOTICE, "Stopping extrapolation thread");
  }
//   memset(&g_delta_buf[0], 0, DELTA_BUF_SIZE*sizeof(double));
  g_delta_fil = 0.;
  g_ddelta_fil = 0.;
  g_deltas_written = 0;
//   g_diter = 0;
  g_ts_count = 0;
  g_crcerr_count = 0;
  pcfgoc();
  if ((result = CreateThread(&g_thread, &time_extrap)) < 0)
  {
    char err_msg[0x100];
    snprintf(err_msg, 0x100,
             "Failed to start new thread. Returned error number %d", result);
    fprintf(stderr, "[%s:%d] %s\n", _FILE_,__LINE__,err_msg);
    if (g_useDb)
        prx_mysql_log(&prx_param, _FILE_, __LINE__, E_NOTICE, err_msg);
    else
        syslog(LOG_NOTICE, "%s", err_msg);
    quit(EXIT_FAILURE);
  }

  g_caught_hup_signal = 0;

  signal(SIGHUP, HupSigHandler);
  return;
}

/*'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''*/
static void pcfgoc()
{
  dictionary *ini;

  if ((ini = iniparser_load("/etc/ntptimed.cfg")) == NULL)
  {
    fprintf(stderr, "[%s:%d] Error: cannot parse cfg file\n", _FILE_, __LINE__);
    quit(EXIT_FAILURE);
  }
#ifdef NTPTIMED_DEBUG
  fprintf(stderr, "[%s:%d] Done: load cfg\n", _FILE_, __LINE__);
#endif

  //if (g_glink_type == SERIAL_GLINK && !g_ser_speed)
  //  g_ser_speed = iniparser_getint(ini, "sync:speed", 115200);
  //if (g_glink_type == SERIAL_GLINK && !strlen(g_ser_dev))
  //  strncpy(g_ser_dev, iniparser_getstring(ini, "sync:dev", "/dev/ttyM1"),
  //          FILENAME_MAX);

  if(!g_isLine)
    strcpy(
        g_line,
        iniparser_getstring(ini,
                            "ntptime:glink",
                            "type:serial,speed:115200,dev:/dev/ttyM1")
        );

  g_extrap_time = iniparser_getdouble(ini, "ntptime:extrap_time", 0.01);
  g_syncTO = iniparser_getdouble(ini, "ntptime:sync_timeout", 10.0);
  g_prx_upd = iniparser_getdouble(ini, "ntptime:upd_time", 3.0);
  syslog_warnTO = iniparser_getdouble(ini, "ntptime:sync_down_wp", 10.0);
  iniparser_freedict(ini);

#ifdef NTPTIMED_DEBUG
  fprintf(stderr, "[%s:%d] Done: read from cfg\n", _FILE_, __LINE__);
  fprintf(stderr, "extrapolation time = %lf\n", g_extrap_time);
  fprintf(stderr, "sync timeout = %lf\n", g_syncTO);
  fprintf(stderr, "prx db update time = %lf\n", g_prx_upd);
  fprintf(stderr, "line = %s\n", g_line);
#endif

  if (g_glink.opened != 0)
  {
    (void)glink_close(&g_glink);
  }
  if (work_mode != WM_ALONE)
  {
    glink_init( &g_glink, g_line );
#ifdef NTPTIMED_DEBUG
    fprintf(stderr, "[%s:%d] Done: open connection\n", _FILE_, __LINE__);
#endif
    (void)glink_set_timeout(&g_glink, g_syncTO/2.);
    glink_flush(&g_glink);
  }

  if (ntptime_shm_init(&g_nsc) < 0)
  {
    fprintf(stderr, "[%s:%d] Error in ntptime_shm_init():\n%s",
            _FILE_, __LINE__, g_nsc.link.err_msg);
    quit(EXIT_FAILURE);
  }
#ifdef NTPTIMED_DEBUG
  fprintf(stderr, "[%s:%d] Done: pcfgoc\n", _FILE_, __LINE__);
#endif
  if (g_useDb)
    prx_mysql_log(&prx_param, _FILE_, __LINE__, E_NOTICE, "Init successful");
  else
    syslog(LOG_NOTICE, "Init successful");
  prx_mysql_set_param_ext(&prx_param, "Enabled", "1");

  return;
}

/*'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''*/
static int CreateThread(pthread_t *thread,void*(*routine)(void*))
{
  if (pthread_create(thread, NULL, routine, NULL))
  {
    switch (errno)
    {
      case EAGAIN:
        fprintf(stderr, "[%s:%d] Insufficient resources\n", _FILE_, __LINE__);
        break;
      case EINVAL:
        fprintf(stderr, "[%s:%d] Invalid settings in attr\n", _FILE_, __LINE__);
        break;
      case EPERM:
        fprintf(stderr,
                "[%s:%d] No permission to set the scheduling policy"
                "and parameters specified in attr\n",
                _FILE_, __LINE__);
        break;
      default:
        fprintf(stderr, "[%s:%d] Unexpected error\n", _FILE_, __LINE__);
    }
    return -1;
  }
#ifdef NTPTIMED_DEBUG
  fprintf(stderr, "[%s:%d] Done: creating thread\n", _FILE_, __LINE__);
#endif
  if (g_useDb)
    prx_mysql_log(&prx_param, _FILE_, __LINE__, E_NOTICE,
            "Starting extrapolation thread");
  else
    syslog(LOG_NOTICE, "Starting extrapolation thread");
  return 0;
}

/*'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''*/
static void extrap(void)
{
  static double curt=0.;
  static double last_prx_upd=0., cur_time=0.;
  static ntptime_shm_t trans;
  double new_delta;

  cur_time = dtime();

  if (pthread_mutex_timedlock(&g_mutex, &g_mutex_to))
  {
      perror("pthread_mutex_timedlock");
      return;
  }
  /* delta extrapolation */
  new_delta = g_delta_fil + g_ddelta_fil*(cur_time-g_last_tsld+(double)NTP_UNIX_DELTA);

  trans.ts_count = g_ts_count;
  trans.crcerr_count = g_crcerr_count;
  if (work_mode == WM_DEFAULT)
    trans.sync_stat = g_sync_stat;
  else
    trans.sync_stat = 0;
  pthread_mutex_unlock(&g_mutex);

  curt = cur_time + (double)NTP_UNIX_DELTA + new_delta;
  trans.delta = new_delta;

  /* Now convert curt to ntptime_t */
  trans.ctime.sec = (u32)curt;
  trans.ctime.psec = (u32)((curt - (double)trans.ctime.sec)*4294967296.0);

  int err;
  if ((err = g_nsc.link.add(&g_nsc.link, &trans, 1)) < 0)
      fprintf(stderr, "[%s:%d] Error in shmbuf_write():\n%s\nfrom shmsbuf: %s",
              _FILE_, __LINE__, shm_sbuf_error(err), g_nsc.link.err_msg
             );
#ifdef NTPTIMED_DEBUG
  fprintf(stderr,
          "\nDaemon: data transmitted to shm\n"
          "time:             %f sec\n"
          "delta:            %f sec\n"
          "total ts:         %u\n"
          "total crc errors: %u\n"
          "sync status:      %d\n",
          curt, trans.delta, trans.ts_count, trans.crcerr_count,
          trans.sync_stat);
#endif
  /* update prx db */
  if (g_useDb && ((cur_time - g_prx_upd) > last_prx_upd))
  {
      char tmp[32];
      ntptime_t delta_tmp;
      int sign;
      if (pthread_mutex_timedlock(&g_mutex, &g_mutex_to))
      {
        perror("pthread_mutex_timedlock");
        return;
      }
      const time_t tmp1 = cur_time + new_delta;
      prx_mysql_set_param_ext(&prx_param, "Sync status", g_sync_stat? "connected":
                                                            "disconnected");
      snprintf(tmp, 32, "%X %X", trans.ctime.sec, trans.ctime.psec);
      prx_mysql_set_param_ext(&prx_param, "NTP time", tmp);
      sign = (trans.delta < 0)? 1: 0;
      delta_tmp.sec = (u32)(sign? -trans.delta: trans.delta);
      delta_tmp.psec = (u32)(((sign? -trans.delta: trans.delta) -
                                          (double)delta_tmp.sec)*4294967296.0);
      snprintf(tmp, 32, "%s%X %X", sign?"-":"", delta_tmp.sec, delta_tmp.psec);
      prx_mysql_set_param_ext(&prx_param, "Delta", tmp);
      prx_mysql_set_param_ext(&prx_param, "Time", ctime(&tmp1));
      snprintf(tmp, 32, "%u", trans.ts_count);
      prx_mysql_set_param_ext(&prx_param, "Total ts", tmp);
      snprintf(tmp, 32, "%u", trans.crcerr_count);
      prx_mysql_set_param_ext(&prx_param, "Total crc", tmp);
      pthread_mutex_unlock(&g_mutex);
      last_prx_upd = cur_time;
  }
}

/*'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''*/
static void* time_extrap(void* arg)
{
  vu_sleep(3.*g_extrap_time); /* wait for first data packets (if any) */

  while (1)
  {
    vu_sleep(g_extrap_time);
    extrap();

    pthread_testcancel();
  }
  pthread_exit(NULL);
}

/*'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''*/
static void dFilter(void)
{
    if (g_deltas_written < koef.total)
    {
        g_delta_fil = g_delta_buf[g_diter];
        g_ddelta_fil = 0.;
    }
    else
    {
        double sum = 0.;
        register unsigned int i = 0;
        for (; i < koef.total; ++i)
        {
            sum += koef.a[i] * g_delta_buf[((g_diter+DELTA_BUF_SIZE)-i)%
                                           DELTA_BUF_SIZE] / g_dfilter_norm;
        }
        g_delta_fil = sum;
        for (i = 0, sum = 0; i < koef.total-1; ++i)
        {
            sum += koef.a[i] * g_ddelta_buf[((g_dditer+(DELTA_BUF_SIZE-1))-i)%
                                          (DELTA_BUF_SIZE-1)] / g_ddfilter_norm;
        }
        g_ddelta_fil = sum;
    }
}

/*'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''*/
static void prx_mysql_set_param_ext(prx_mysql_context *cont, const char *name,
                              const char *val)
{
    if (!g_useDb)
        return;

    int err;
    err = prx_mysql_set_param(cont, name, val);
    if (err < 0)
    {
        if (err == EMYSQLQUERYFAILED)
            prx_mysql_set_param_ext(cont, name, val);
        else
        {
            fprintf(stderr, "[%s:%d] Warning %d: %s\n",
                    _FILE_, __LINE__, err, cont->err_msg);
            prx_mysql_log(cont, _FILE_, __LINE__, E_ERROR, cont->err_msg);
        }
    }
}
