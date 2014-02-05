#ifndef _AppsvrMsghandlerH_

typedef struct MessageElmt{
	int clientWorkerId;
	int clientId;
	int clientType;
	char	*msgBuf;
	int		msgLen;
	struct MessageElmt *next;	
}MessageElmt;

typedef struct MessageQueue{
	pthread_mutex_t    lock;
	pthread_cond_t     flag;
	struct MessageElmt *head;	
	struct MessageElmt *tail;	
}MessageQueue;

#define _AppsvrMsghandlerH_
#endif//_AppsvrMsghandlerH_
