
#include <process.h>
#include <errno.h>
#include "include\smct.h"
#include "include/appsvr.h"
#include "include/appsvr_thread.h"

#pragma warning(disable : 4996)  // deprecated CRT function

#define THREAD_STACKSIZE 256 * 1024    /* 256kB */


#ifdef WIN32

int smctPthreadCreate(pthread_t *thread, pthread_attr_t *attr, 
                        void (*start_routine)(void *), void *data, 
                        size_t stacksize, int detachstate){

	*thread = (pthread_t) _beginthread( start_routine, (int)stacksize, data);

	return 0;
}

#else

int smctPthreadCreate(pthread_t *thread, pthread_attr_t *attr, 
                        void *(*start_routine)(void *), void *data, 
                        size_t stacksize, int detachstate){
	pthread_attr_t lattr;
        
  if (!attr) {
    pthread_attr_init(&lattr);
    attr = &lattr;
  }
#ifdef __linux__
	/* On Linux, pthread_attr_init() defaults to PTHREAD_EXPLICIT_SCHED,
	   which is kind of useless. Change this here to
	   PTHREAD_INHERIT_SCHED; that way the -p option to set realtime
	   priority will propagate down to new threads by default.
	   This does mean that callers cannot set a different priority using
	   PTHREAD_EXPLICIT_SCHED in the attr argument; instead they must set
	   the priority afterwards with pthread_setschedparam(). */
	errno = pthread_attr_setinheritsched(attr, PTHREAD_INHERIT_SCHED);        
#endif

	if (!stacksize)
  	stacksize = THREAD_STACKSIZE;  
  errno = pthread_attr_setstacksize(attr, stacksize);
  
  if (detachstate == PTHREAD_CREATE_DETACHED)  
  	errno = pthread_attr_setdetachstate(attr, PTHREAD_CREATE_DETACHED);
        
  return pthread_create(thread, attr, start_routine, data); /* We're in ast_pthread_create, so it's okay */
}

#endif
