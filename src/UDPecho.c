#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "tthread.h"

extern int  connectUDP(const char *host, const char *service);
extern int  errexit(const char *format, ...);

#ifndef MILLISEC
#define MILLISEC 1000
#endif
#ifndef MICROSEC
#define MICROSEC 1000000
#endif

#define BUFSIZE 1024

typedef struct _EchoInfo {
   int               sock;
   unsigned          addr;
   unsigned          port;
   time_t            start;      /* time this thread began */
   unsigned          rt_time;    /* cumulative round trip measured in ms */
   unsigned          sent;       /* number of packets sent */
   unsigned          rcvd;       /* number of packets rcvd */
   unsigned          timeout;    /* ms */
   unsigned          running;    /* thread still running */
   unsigned          load;       /* target send load in kbps */
   struct _EchoInfo  *next;      /* linked list */
} EchoInfo;

static EchoInfo   *echoList = NULL;

static void       addThread(char *addr, char *port);
static void       *echoThread(EchoInfo *);
static EchoInfo   *getInfo(char *addr, char *port);
static void       showStats(char *what);
static void       printhelp(void);
static unsigned   subtract_timeval(struct timeval *tv1, struct timeval *tv2);

static unsigned timeout = MILLISEC;        /* 1 sec */
static unsigned loadkpbs = 1024 * 10;  /* 10 mbits/sec  */

int main(int argc, char *argv[])
{
   char     hostname[100], prompt[100], rbuf[500], *s;
   char     addrstr[100], portstr[100];
   int      i, n;
   Thread   thr;
   FILE     *fp;
   unsigned tout, load;

   for (i = 1; i < argc; i++) {
      if (strncmp(argv[i], "-h", 2) == 0) {
         errexit("usage: UDPecho [-t timeout(ms)] [-l load(kbs)] [addressfile ...]\n");
      }
      else if (strcmp(argv[i], "-t") == 0) {
         tout = strtoul(argv[++i], (char **)NULL, 10);
         if (tout != ULONG_MAX)
            timeout = tout;
         else
            printf("Bogus timeout value: %s\n", argv[i]);
      }
      else if (strcmp(argv[i], "-l") == 0) {
         load = strtoul(argv[++i], (char **)NULL, 10);
         if (load != ULONG_MAX)
            loadkpbs = load;
         else
            printf("Bogus load value: %s\n", argv[i]);
      }
      else {
         fp = fopen(argv[i], "r");
         if (fp == NULL)
            errexit("Can't open file %s\n", argv[i]);
         do {
            n = fscanf(fp, "%s %s", &addrstr, &portstr);
            if (n == 2)
               addThread(addrstr, portstr);
         } while (n != EOF);
         fclose(fp);
      }
   }

   if (gethostname(hostname, 100) < 0) {
      strcpy(hostname, "unknown");
   }
   strcpy(prompt, "[UDPecho on ");
   strcat(prompt, hostname);
   strcat(prompt, "]");
   while (1) {
      printf(prompt);
      if (!fgets(rbuf, 500, stdin)) {
         printf("Bad input read ... bye\n");
         exit(0);
      }
      for (s = rbuf; *s; s++)
         if (*s == '\r' || *s == '\n')
            *s = 0;
      
      if (strcmp(rbuf, "exit") == 0 || strcmp(rbuf, "quit") == 0) {
         showStats("all");
         exit(0);
      }
      else if (strncmp(rbuf, "stat ", 5) == 0) {
         showStats(rbuf + 5);
      }
      else if (strncmp(rbuf, "add ", 4) == 0) {
         n = sscanf(rbuf + 4, "%s %s", &addrstr, &portstr);
         if (n == 2)
            addThread(addrstr, portstr);
         else {
            printf("scanned only %d\n", n);
            printhelp();
         }
      }
      else {
         printhelp();
      }
   }
   return 0;
}

static void addThread(char *addrstr, char *portstr)
{
   int            sock;
   unsigned       addr, port;
   unsigned char  *bp;
   EchoInfo       *ei;
   Thread         thr;
   
   addr = inet_addr(addrstr);
   if (addr == -1) {
      printf("Totally bogus ip address %s\n", addrstr);
      return;
   }
   port = strtoul(portstr, NULL, 10);
   if (port == ULONG_MAX) {
      printf("Totally bogus port %s\n", portstr);
      return;
   }
   sock = connectUDP(addrstr, portstr);
   
   ei = (EchoInfo *)malloc(sizeof(EchoInfo));
   memset(ei, 0, sizeof(EchoInfo));
   ei->sock = sock;
   ei->addr = addr;
   ei->port = port;
   ei->timeout = timeout;
   ei->load = loadkpbs;
   ei->next = echoList;
   echoList = ei;

   thread_create(&thr, (ThreadRunFunc)echoThread, ei);
}

static void *echoThread(EchoInfo *ei)
{
   char              buf[BUFSIZE];
   struct timeval    tv, stv;
   struct timezone   tz;
   struct in_addr    iaddr;
   unsigned          seq = 0, *uptr, rttime;
   fd_set            rfds;
   int               ret, n;
   int               outofseq = 0;
   unsigned          ltime, wtime;
   Mutex             mutex;   /* for pausing */
   Condition         cond;    /* for pausing */
   struct timespec   waittime;
   
   uptr = (unsigned *)buf;

   mutex_create(&mutex);
   mutex_lock(&mutex);
   cond_create(&cond);

   ei->running = 1;
   ei->start = time(NULL);

   while (ei->running) {
      if (!outofseq) {
         *uptr = ++seq;
         gettimeofday(&tv, &tz);
         memcpy(uptr + 1, &tv, sizeof(struct timeval));
         send(ei->sock, buf, BUFSIZE, 0);
         ei->sent++;
      }
      FD_ZERO(&rfds);
      FD_SET(ei->sock, &rfds);
      stv.tv_sec = ei->timeout / MILLISEC;
      stv.tv_usec = (ei->timeout % MILLISEC) * MILLISEC;
      ret = select(ei->sock + 1, &rfds, NULL, NULL, &stv);
      if (ret > 0) {
         n = 0;
         while (n < BUFSIZE) {
            ret = recv(ei->sock, buf + n, BUFSIZE - n, 0);
            if (ret < 0) {
               iaddr.s_addr = ei->addr;
               printf("Error reading sock for %s\n", inet_ntoa(iaddr));
               ei->running = 0;
               break;
            }
            n += ret;
         }
         if (n == BUFSIZE) {
            outofseq = (*uptr < seq) ? 1 : 0;
            memcpy(&tv, uptr + 1, sizeof(struct timeval));
            gettimeofday(&stv, &tz);
            rttime = subtract_timeval(&stv, &tv);
            ei->rt_time += rttime;
            ei->rcvd++;
         }
      }
      else if (ret == 0) {
         printf("Timeout\n");
      }
      else {
         fprintf(stderr, strerror(errno));
      }
      ltime = time(NULL) - ei->start;
      if (ltime && (((ei->sent * 8) / ltime) > ei->load)) {
         wtime = ((ei->sent * 8) / ei->load) - ltime;
         if (wtime > 0) {
            waittime.tv_sec = wtime;
            waittime.tv_nsec = 0;
            printf("waiting for %d seconds\n", wtime);
            sleep(wtime);
            /* cond_timedwait(&cond, &mutex, &waittime); */
            printf("done waiting\n", wtime);
         }
      }
   }
   iaddr.s_addr = ei->addr;
   printf("... closing sock and exiting thread for %s\n", inet_ntoa(iaddr));
   close(ei->sock);
   mutex_destroy(&mutex);
   cond_destroy(&cond);
}
      
   
static void printhelp(void)
{
   printf("add ipaddress port    - adds a thread\n");
   printf("stat ipaddress port   - shows stats for an ipaddress port\n");
   printf("stat sum              - summary stats\n");
   printf("stat all              - shows stats for all \n");
   printf("help                  - shows this\n");
   printf("aksjdfhlaksd          - shows this\n");
   printf("exit                  - exits\n");
}

static void showStats(char *what)
{
   EchoInfo *ei;
   int      all, bps, count;
   time_t   ttime, atime, mintime, cumtime;
   unsigned addr, packets_sent, packets_rcvd, latency, totlatency;
   struct in_addr iaddr;
   char     *s, addrbuf[100], portbuf[100];
   
   if (strcmp(what, "all") == 0 || strcmp(what, "sum") == 0) {
      all = strcmp(what, "all") == 0;
      count = 0;
      mintime = LONG_MAX;
      packets_sent = packets_rcvd = 0;
      cumtime = 0;
      for (ei = echoList; ei; ei = ei->next) {
         count++;
         atime = time(NULL) - ei->start;
         packets_sent += ei->sent;
         packets_rcvd += ei->rcvd;
         cumtime += atime;
         if (ei->rt_time && ei->rcvd)
            totlatency += ei->rt_time / ei->rcvd;
         if (ei->start < mintime)
            mintime = ei->start;
         if (all) {
            iaddr.s_addr = ei->addr;
            latency = ei->rcvd ? ei->rt_time / ei->rcvd : 0;
            printf("%15s sent %10d rcvd %10d latency %5d ms rate %d kbps\n",
                   inet_ntoa(iaddr), ei->sent, ei->rcvd,
                   latency,
                   ((ei->rcvd * 8)/ atime));
         }
      }
      latency = packets_rcvd ? totlatency / packets_rcvd : 0;
      printf("---------------------------------------------------\n");
      printf("Number of addresses:  %d\n", count);
      printf("Packets sent:         %d\n", packets_sent);
      printf("Packets rcvd:         %d\n", packets_rcvd);
      printf("Average latency:      %d\n", latency);
      printf("Average kbps:         %d\n", (packets_rcvd * 8) / cumtime);
   }
   else {
      sscanf(what, "%s %s", addrbuf, portbuf);
      addr = inet_addr(addrbuf);
      if (addr == -1) {
         printf("Bogus ip address: %s\n", s);
      }
      else {
         ei = getInfo(addrbuf, portbuf);
         if (ei) {
            iaddr.s_addr = ei->addr;
            atime = time(NULL) - ei->start;
            latency = ei->rcvd ? ei->rt_time / ei->rcvd : 0;
            printf("%15s sent %10d rcvd %10d latency %5d ms rate %d kbps\n",
                   inet_ntoa(iaddr), ei->sent, ei->rcvd,
                   latency,
                   ((ei->rcvd * 8)/ atime));
         }
         else {
            printf("Don't know nothin bout no address %s\n", what);
         }
      }
   }
}

static EchoInfo *getInfo(char *addrstr, char *portstr)
{
   EchoInfo       *ei;
   unsigned       addr, port;
   
   addr = inet_addr(addrstr);
   if (addr == -1) {
      printf("Totally bogus ip address %s\n", addrstr);
      return;
   }
   port = strtoul(portstr, NULL, 10);
   if (port == ULONG_MAX) {
      printf("Totally bogus port %s\n", portstr);
      return;
   }
   for (ei = echoList; ei; ei = ei->next) {
      if (ei->addr == addr && ei->port == port)
         break;
   }
   return ei;
}

/* subtracts tv2 from tv1 and returns the result in milliseconds */
static unsigned subtract_timeval(struct timeval *tv1, struct timeval *tv2)
{
   unsigned us;
   unsigned ms = (tv1->tv_sec - tv2->tv_sec) * MILLISEC;
   

   if (tv1->tv_usec < tv2->tv_usec) {
      us = (tv1->tv_usec + MICROSEC) - tv2->tv_usec;
      ms = (ms - MILLISEC) + (us / MILLISEC);
   }
   else {
      ms += (tv1->tv_usec - tv2->tv_usec) / MILLISEC;
   }
   return ms;
}
