
#ifndef _AppsvrThreadH_
#define _AppsvrThreadH_


#define smctPthreadCreateDetached(a,b,c,d) smctPthreadCreate(a,b,c,d,0,PTHREAD_CREATE_DETACHED)
int smctPthreadCreate(pthread_t *thread, pthread_attr_t *attr, 
                        void (*start_routine)(void *), void *data, 
                        size_t stacksize, int detachstate);

#endif//_AppsvrThreadH_
