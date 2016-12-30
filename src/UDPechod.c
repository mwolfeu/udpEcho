#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "tthread.h"

extern int  passiveUDP(const char *service);
extern int  errexit(const char *format, ...);

#define BUFSIZE 4096

typedef struct _AddrStat {
   unsigned          addr;
   unsigned          bytes;
   unsigned          packets;
   time_t            start;
   struct _AddrStat  *next;      /* for hash */
   struct _AddrStat  *nextlink;  /* for global linked list */
} AddrStat;

static AddrStat *addrHash[256][256];
static AddrStat *addrList = NULL;

static void *statThread(char *);
static void addStat(unsigned addr, unsigned bytes);
static AddrStat *getStat(unsigned addr);
static void showStats(char *what);

int main(int argc, char *argv[])
{
   struct sockaddr_in   fsin;
   char     buf[BUFSIZE], *port;
   int      sock;
   int      alen;
   int      bytes, i, j;
   Thread   thr;

   if (argc < 2)
      errexit("usage: UDPechod port\n");

   memset(addrHash, 0, sizeof(AddrStat *) * 256 * 256);

   port = argv[1];
   
   sock = passiveUDP(port);

   thread_create(&thr, (ThreadRunFunc)statThread, port);
   
   while (1) {
      alen = sizeof(fsin);
      bytes = recvfrom(sock, buf, BUFSIZE, 0,
                       (struct sockaddr *)&fsin, &alen);
      if (bytes < 0)
         errexit("recvfrom: %s\n", strerror(errno));

      sendto(sock, (char *)buf, bytes, 0,
             (struct sockaddr *)&fsin, sizeof(fsin));

      addStat(fsin.sin_addr.s_addr, bytes);
   }
   return 0;
}

static void addStat(unsigned addr, unsigned bytes)
{
   unsigned char *bp = (unsigned char *)&addr;
   AddrStat *sp, *sp2;

   sp = addrHash[bp[2]][bp[3]];
   
   if (sp) {
      do {
         if (sp->addr == addr) {
            sp->bytes += bytes;
            sp->packets++;
            return;
         }
         if (sp->next == NULL) {
            sp2 = (AddrStat *)malloc(sizeof(AddrStat));
            sp2->addr = addr;
            sp2->bytes = bytes;
            sp2->packets = 1;
            sp2->start = time(NULL);
            sp2->next = NULL;
            
            sp->next = sp2;

            sp2->nextlink = addrList;
            addrList = sp2;
            
            return;
         }
         sp = sp->next;
      }  while (sp);
   }
   else {
      sp = (AddrStat *)malloc(sizeof(AddrStat));
      sp->addr = addr;
      sp->bytes = bytes;
      sp->packets = 1;
      sp->start = time(NULL);
      sp->next = NULL;
      addrHash[bp[2]][bp[3]] = sp;
      sp->nextlink = addrList;
      addrList = sp;
   }
}

static AddrStat *getStat(unsigned addr)
{
   unsigned char *bp = (unsigned char *)&addr;
   AddrStat *sp = addrHash[bp[2]][bp[3]];
   if (sp) {
      do {
         if (sp->addr == addr)
            break;
         sp = sp->next;
      }  while (sp);
   }
   return sp;
}

static void printhelp(void)
{
   printf("stat <ipaddress>   - shows stats for an ipaddress\n");
   printf("stat sum           - summary stats\n");
   printf("stat all           - shows stats for all ipaddresses\n");
   printf("help               - shows this\n");
   printf("aksjdfhlaksd       - shows this\n");
   printf("exit               - exits\n");
}

static void showStats(char *what)
{
   AddrStat *sp;
   int      i , j, all, bps, kbits, count, packets;
   time_t   ttime, atime, mintime;
   double   tbytes, tbps;
   unsigned addr;
   struct in_addr iaddr;
   char     *s;
   
   if (strcmp(what, "all") == 0 || strcmp(what, "sum") == 0) {
      all = strcmp(what, "all") == 0;
      count = 0;
      packets = 0;
      tbytes = 0;
      mintime = LONG_MAX;
      for (sp = addrList; sp; sp = sp->nextlink) {
         count++;
         atime = time(NULL) - sp->start;
         kbits = (sp->bytes / 1024) * 8;
         bps = kbits / atime;
         tbytes += sp->bytes;
         packets += sp->packets;
         if (sp->start < mintime)
            mintime = sp->start;
         if (all) {
            iaddr.s_addr = sp->addr;
            printf("%20s got %10d packets - %10d kbits at %10d kbps\n",
                   inet_ntoa(iaddr), sp->packets, kbits, bps);
         }
      }
      printf("---------------------------------------------------\n");
      printf("Number of addresses:  %d\n", count);
      printf("Total Packets:        %d\n", packets);
      printf("Total Throughput:     %d mbps\n",
             (int)(((tbytes * 8) / ((time(NULL) - mintime))) / 0x100000));
      printf("Average Throughput:   %d kbps\n",
             (int)(((tbytes * 8 * 1024) / ((time(NULL) - mintime))) /
                   (count * 0x100000)));
   }
   else {
      addr = inet_addr(what);
      if (addr == -1) {
         printf("Bogus ip address: %s\n", s);
      }
      else {
         sp = getStat(addr);
         if (sp) {
            atime = time(NULL) - sp->start;
            kbits = (sp->bytes / 1024) * 8;
            bps = kbits / atime;
            iaddr.s_addr = sp->addr;
            printf("%20s got %10d packets - %10d kbits at %10d kbs\n",
                   inet_ntoa(iaddr), sp->packets, kbits, bps);
         }
         else {
            printf("Don't know nothin bout no address %s\n", what);
         }
      }
   }
}

static void *statThread(char *port)
{
   char hostname[100], prompt[100], rbuf[500], *s;

   if (gethostname(hostname, 100) < 0) {
      strcpy(hostname, "unknown");
   }
   strcpy(prompt, "[UDPechod on ");
   strcat(prompt, hostname);
   strcat(prompt, " port ");
   strcat(prompt, port);
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
      else {
         printhelp();
      }
   }
   return NULL;
}
