#ifndef __TTHREAD_H__
#define __TTHREAD_H__

#include <pthread.h>

typedef pthread_mutex_t Mutex;
typedef pthread_cond_t  Condition;
typedef pthread_t       Thread;

typedef void *          (*ThreadRunFunc)(void *);

extern int  thread_create(Thread *thr, ThreadRunFunc runfunc, void *funcdata);
extern void thread_printerr(const char *string, int err);
extern int  mutex_create(Mutex *);
extern int  mutex_destroy(Mutex *);
extern int  mutex_lock(Mutex *);
extern int  mutex_unlock(Mutex *);
extern int  cond_create(Condition *);
extern int  cond_destroy(Condition *);
extern int  cond_signal(Condition *);
extern int  cond_wait(Condition *, Mutex *);
extern int  cond_timedwait(Condition *, Mutex *, const struct timespec *);


#endif
