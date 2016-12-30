#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "tthread.h"

extern int  connectUDP(const char *host, const char *service);
extern int  errexit(const char *format, ...);

#ifndef MILLISEC
#define MILLISEC 1000
#endif
#ifndef MICROSEC
#define MICROSEC 1000000
#endif

#define BUFSIZE   1024

typedef struct _EchoInfo {
   unsigned          addr;
   unsigned          port;
   time_t            start;      /* time sending began */
   unsigned          rt_time;    /* cumulative round trip measured in ms */
   unsigned          sent;       /* number of packets sent */
   unsigned          rcvd;       /* number of packets rcvd */
   unsigned          seq;        /* latest sequence received */
   unsigned          outOfseq;   /* packets received out of sequence */
   struct _EchoInfo  *next;      /* for hash */
   struct _EchoInfo  *nextlink;  /* global linked list */
} EchoInfo;

static EchoInfo   *echoHash[256][256];
static EchoInfo   *echoList = NULL;
static Mutex      recvMutex;
static Mutex      sendMutex;
static Condition  sendStart;

static void       addEcho(char *addrstr, char *portstr);
static void       delEcho(char *addrstr, char *portstr);
static void       *sendThread(int sock);
static void       *recvThread(int sock);
static EchoInfo   *findInfo(char *addrstr, char *portstr);
static EchoInfo   *getInfo(struct sockaddr_in  *fsin);
static void       showStats(char *what);
static void       printhelp(void);
static unsigned   subtract_timeval(struct timeval *tv1, struct timeval *tv2);
static int        up(char *addrstr);
static int        down(char *addrstr);
static void       downall(void);
static int        launch(char *argv[]);

static unsigned timeout = MILLISEC;        /* 1 sec */
static unsigned loadkpbs = 1024 * 10;  /* 10 mbits/sec  */
static char     *bind_port = "3333";

int main(int argc, char *argv[])
{
   char     hostname[100], prompt[100], rbuf[500], *s;
   char     addrstr[100], portstr[100];
   int      i, n, sock;
   Thread   thr;
   FILE     *fp;
   unsigned tout, load;
   EchoInfo *ei;
   struct sockaddr_in sin; /* an Internet endpoint address  */


   memset(echoHash, 0, sizeof(echoHash));

   mutex_create(&sendMutex);
   mutex_create(&recvMutex);
   cond_create(&sendStart);

   for (i = 1; i < argc; i++) {
      if (strncmp(argv[i], "-h", 2) == 0) {
         errexit("usage: UDPecho [-p localport] [-t timeout(ms)] [-l load(kbs)] [addressfile ...]\n");
      }
      else if (strcmp(argv[i], "-t") == 0) {
         tout = strtoul(argv[++i], (char **)NULL, 10);
         if (tout != ULONG_MAX)
            timeout = tout;
         else
            printf("Bogus timeout value: %s\n", argv[i]);
      }
      else if (strcmp(argv[i], "-p") == 0) {
         bind_port = argv[++i];
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
               addEcho(addrstr, portstr);
         } while (n != EOF);
         fclose(fp);
      }
   }
   sock = passiveUDP(bind_port);
   
   thread_create(&thr, (ThreadRunFunc)sendThread, (void *)sock);
   thread_create(&thr, (ThreadRunFunc)recvThread, (void *)sock);

   /* interactive loop */
   
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
         downall();
         exit(0);
      }
      else if (strncmp(rbuf, "stat ", 5) == 0) {
         showStats(rbuf + 5);
      }
      else if (strncmp(rbuf, "add ", 4) == 0) {
         n = sscanf(rbuf + 4, "%s %s", &addrstr, &portstr);
         if (n == 2)
            addEcho(addrstr, portstr); 
         else {
            printf("scanned only %d\n", n);
            printhelp();
         }
      }
      else if (strncmp(rbuf, "del ", 4) == 0) {
         n = sscanf(rbuf + 4, "%s %s", &addrstr, &portstr);
         if (n == 2)
            delEcho(addrstr, portstr); 
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

static void addEcho(char *addrstr, char *portstr)
{
   int            sock;
   unsigned       addr, port;
   unsigned char  *bp;
   EchoInfo       *ei, *eio;
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

   up(addrstr);

   ei = (EchoInfo *)malloc(sizeof(EchoInfo));
   memset(ei, 0, sizeof(EchoInfo));
   ei->addr = addr;
   ei->port = port;

   mutex_lock(&sendMutex);
   mutex_lock(&recvMutex);

   ei->start = time(NULL);
   ei->nextlink = echoList;
   echoList = ei;

   bp = (unsigned char *)&addr;
   eio = echoHash[bp[2]][bp[3]];
   if (eio) {
      while (eio->next)
         eio = eio->next;
      eio->next = ei;
   }
   else {
      echoHash[bp[2]][bp[3]] = ei;
   }

   cond_signal(&sendStart);
   mutex_unlock(&sendMutex);
   mutex_unlock(&recvMutex);
}

static void delEcho(char *addrstr, char *portstr)
{
   unsigned       addr, port;
   unsigned char  *bp;
   EchoInfo       *ei, *eio;

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

   mutex_lock(&sendMutex);
   mutex_lock(&recvMutex);
   
   bp = (unsigned char *)&addr;
   ei = echoHash[bp[2]][bp[3]];
   if (!ei) {
      printf("Can't find %s %s\n", addrstr, addrstr);
      return;
   }
   if (ei->addr == addr && ei->port == port) {
      echoHash[bp[2]][bp[3]] = NULL;
   }
   else {
      int found = 0;
      while(ei->next) {
         eio = ei;
         ei = ei->next;
         if (ei->addr == addr && ei->port == port) {
            eio->next = ei->next;
            found = 1;
            break;
         }
      }
      if (!found) {
         printf("Can't find %s %s\n", addrstr, addrstr);
         return;
      }
   }
   /* fix up the links */
   if (ei == echoList) {
      echoList = echoList->nextlink;
      free(ei);
   }
   else {
      for (eio = echoList; eio; eio = eio->nextlink) {
         if (eio->nextlink == ei) {
            eio->nextlink = ei->nextlink;
            free(ei);
         }
      }
   }
   mutex_unlock(&sendMutex);
   mutex_unlock(&recvMutex);

   down(addrstr);
}

static void *sendThread(int sock)
{
   char                 buf[BUFSIZE];
   struct timeval       tv;
   struct timezone      tz;
	struct sockaddr_in   toaddr;
   unsigned             seq = 0, *uptr, rttime;
   int                  ret, n;
   unsigned             start, ltime, wtime, packets;
   struct timespec      waittime;
   EchoInfo             *ei;
   
   uptr = (unsigned *)buf;

   mutex_lock(&sendMutex);
   if (!echoList)
      cond_wait(&sendStart, &sendMutex);
   start = time(NULL);
   packets = 0;

   while (1) {
      for (ei = echoList; ei; ei = ei->nextlink) {
         *uptr = ++seq;
         gettimeofday(&tv, &tz);
         memcpy(uptr + 1, &tv, sizeof(struct timeval));
         toaddr.sin_addr.s_addr = ei->addr;
         toaddr.sin_port = htons(ei->port);
         toaddr.sin_family = AF_INET;
         sendto(sock, buf, BUFSIZE, 0,
                (struct sockaddr *)&toaddr, sizeof(toaddr));
         ei->sent++;
      }
      packets++;
      mutex_unlock(&sendMutex);
      
      ltime = time(NULL) - start;
      if (ltime == 0)
         ltime = 1;  /* force a compare */
      if (((packets * 8) / ltime) > loadkpbs) {
         wtime = ((packets * 8) / loadkpbs) - ltime;
         if (wtime > 0) {
            /* printf("waiting for %d seconds ... sent %d\n", wtime, packets);
             */
            sleep(wtime);
            /* printf("done waiting\n", wtime);
             */
         }
      }
      mutex_lock(&sendMutex);
   }
   printf("... exiting send thread\n");
}
      
static void *recvThread(int sock)
{
   char                 buf[BUFSIZE];
   struct timeval       tv, stv;
   struct timezone      tz;
   unsigned             *uptr, rttime;
   int                  ret, n;
   unsigned             ltime, wtime;
	struct sockaddr_in   fsin;	   /* the request from address	*/
	int                  alen;    /* from-address length		*/
   EchoInfo             *ei;
   
   uptr = (unsigned *)buf;

   while (1) {
      n = 0;
      while (n < BUFSIZE) {
         alen = sizeof(fsin);
         ret = recvfrom(sock, buf + n, BUFSIZE - n, 0,
                        (struct sockaddr *)&fsin, &alen);
         if (ret < 0) {
            printf("Error reading sock for %s\n", inet_ntoa(fsin.sin_addr));
            break;
         }
         n += ret;
      }
      if (n == BUFSIZE) {
         mutex_lock(&recvMutex);
         ei = getInfo(&fsin);
         if (ei) {
            if (ei->seq == 0) {
               ei->seq = *uptr;
            }
            else {
               if (*uptr != (ei->seq + 1))
                  ei->outOfseq++;
               ei->seq = *uptr;
            }
            memcpy(&tv, uptr + 1, sizeof(struct timeval));
            gettimeofday(&stv, &tz);
            rttime = subtract_timeval(&stv, &tv);
            ei->rt_time += rttime;
            ei->rcvd++;
         }
         mutex_unlock(&recvMutex);
      }
   }
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
   
   mutex_lock(&sendMutex);
   mutex_lock(&recvMutex);

   if (strcmp(what, "all") == 0 || strcmp(what, "sum") == 0) {
      all = strcmp(what, "all") == 0;
      count = 0;
      mintime = LONG_MAX;
      packets_sent = packets_rcvd = 0;
      cumtime = 0;
      for (ei = echoList; ei; ei = ei->nextlink) {
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
         ei = findInfo(addrbuf, portbuf);
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

   mutex_unlock(&sendMutex);
   mutex_unlock(&recvMutex);
}

static EchoInfo *getInfo(struct sockaddr_in  *fsin)
{
   unsigned char  *bp;
   EchoInfo       *ei;
   unsigned       addr = fsin->sin_addr.s_addr;
   unsigned       port = ntohs(fsin->sin_port);

   bp = (unsigned char *)&fsin->sin_addr.s_addr;
   for (ei = echoHash[bp[2]][bp[3]]; ei; ei = ei->next) {
      if (ei->addr == addr && ei->port == port)
         break;
   }
   return ei;
}

static EchoInfo *findInfo(char *addrstr, char *portstr)
{
   unsigned char  *bp;
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
   bp = (unsigned char *)&addr;
   for (ei = echoHash[bp[2]][bp[3]]; ei; ei = ei->next) {
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

static int up(char *addrstr)
{
   static int     addCnt = 0;
   char           *argv[6];
   char           buff[20];

   sprintf(buff, "%d", addCnt++);
   argv[0] = "sh";
   argv[1] = "up";
   argv[2] = addrstr;
   argv[3] = buff;
   argv[4] = 0;
   return launch(argv);
}

static int down(char *addrstr) 
{
   char *argv[5];

   argv[0] = "sh";
   argv[1] = "down";
   argv[2] = addrstr;
   argv[3] = 0;
   return launch(argv);
}

static void downall(void)
{
   EchoInfo *ei;
	struct in_addr   inaddr;

   for (ei = echoList; ei; ei = ei->nextlink) {
      inaddr.s_addr = ei->addr;
      down(inet_ntoa(inaddr));
   }
}

static int launch(char *argv[])
{
   int pid, status;

   pid = fork();
   if (pid == -1)
      return -1;
   if (pid == 0) {
      execv("/bin/sh", argv);
      exit(127);
   }
   do {
      if (waitpid(pid, &status, 0) == -1) {
         if (errno != EINTR)
            return -1;
      } else
         return status;
   } while(1);
}
