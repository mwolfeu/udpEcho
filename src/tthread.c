#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "tthread.h"


int thread_create(Thread *thr, ThreadRunFunc runfunc, void *funcdata)
{
   pthread_attr_t attr;
   int            err;

   err = pthread_attr_init(&attr);
   if (err) {
      thread_printerr("pthread_attr_init", err);
      return 0;
   }
   err = pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
   if (err) {
      thread_printerr("pthread_attr_setscope", err);
      return 0;
   }
   err = pthread_create(thr, &attr, runfunc, funcdata);
   if (err) {
      thread_printerr("pthread_create", err);
      return 0;
   }
   return 1;
}

int mutex_create(Mutex *m)
{
   int err = pthread_mutex_init(m, NULL);
   if (err) {
      thread_printerr("pthread_mutex_init", err);
      return 0;
   }
   return 1;
}

int mutex_destroy(Mutex *m)
{
   int err = pthread_mutex_destroy(m);
   if (err) {
      thread_printerr("pthread_mutex_destroy", err);
      return 0;
   }
   return 1;
}

int mutex_lock(Mutex *m)
{
   int err = pthread_mutex_lock(m);
   if (err) {
      thread_printerr("pthread_mutex_lock", err);
      return 0;
   }
   return 1;
}

int mutex_unlock(Mutex *m)
{
   int err = pthread_mutex_unlock(m);
   if (err) {
      thread_printerr("pthread_mutex_unlock", err);
      return 0;
   }
   return 1;
}

int cond_create(Condition *c)
{
   int err = pthread_cond_init(c, NULL);
   if (err) {
      thread_printerr("pthread_cond_init", err);
      return 0;
   }
   return 1;
}

int cond_destroy(Condition *c)
{
   int err = pthread_cond_destroy(c);
   if (err) {
      thread_printerr("pthread_cond_destroy", err);
      return 0;
   }
   return 1;
}

int cond_signal(Condition *c)
{
   int err = pthread_cond_signal(c);
   if (err) {
      thread_printerr("pthread_cond_signal", err);
      return 0;
   }
   return 1;
}

int cond_wait(Condition *c, Mutex *m)
{
   int err = pthread_cond_wait(c, m);
   if (err) {
      thread_printerr("pthread_cond_wait", err);
      return 0;
   }
   return 1;
}

int cond_timedwait(Condition *c, Mutex *m, const struct timespec *ts)
{
   int etime = pthread_cond_timedwait(c, m, ts);
   if (etime == ETIME) {   /* timedout */
      return 0;
   }
   return 1;
}

void thread_printerr(const char *string, int err)
{
   switch (err) {
      case EFAULT:
         printf("Error in %s: Bad Address\n", string);
         break;
      case EINVAL:
         printf("Error in %s: Bad Value\n", string);
         break;
      case EINTR:
         printf("Error in %s: Signal interrupt\n", string);
         break;
      case EAGAIN:
         printf("Error in %s: System resources exceeded\n", string);
         break;
      case ENOMEM:
         printf("Error in %s: Insufficient memory\n", string);
         break;
      default:
         printf("Error in %s: Unkown Error %d\n", string, err);
         break;
   }
}
