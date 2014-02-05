#ifndef _AppsvrClientH_
#define _AppsvrClientH_



#define CLIENT_BUFFER_SIZE	2048
#define MAX_CLIENT					1024

typedef struct ClientSession{
	int type;														/* client type */
	
	/* connection data */
	int  sock;														/* socket handle */
	char readBuf[CLIENT_BUFFER_SIZE];	  /* implement circular buffer to avoid data shifting */
	int  readBufSize;
	char writeBuf[CLIENT_BUFFER_SIZE];	
	int  writeBufSize;
	/* write mutex */
	
	struct ClientSession *prev;
	struct ClientSession *next;
}ClientSession;

typedef struct ClientSessionList{
	int size;
	pthread_mutex_t lock;
	ClientSession *first;
	ClientSession *last;
}ClientSessionList;


#define WORKER_MAX_CAPACITY		64

typedef struct ClientMessage{
	int threadId;
	int clientId;
	char message[4096];
	int messageLen;
	
	struct ClientMessage *prev;
	struct ClientMessage *next;
}ClientMessage;

typedef struct ClientMessageList{
	int size;
	pthread_mutex_t lock;
	ClientMessage *first;
	ClientMessage *last;
}ClientMessageList;

typedef struct ClientWorkerThread{
	pthread_t threadId;	
	ClientSessionList	*clients;		/* daftar klien yang sedang dihandle */
	ClientMessageList *messages;  /* daftar message yang perlu dikirim ke klien */
	
	struct ClientWorkerThread *prev;
	struct ClientWorkerThread *next;
}ClientWorkerThread;

typedef struct ClientWorkerThreadList{
	int size;
	ClientWorkerThread *first;
	ClientWorkerThread *last;
}ClientWorkerThreadList;

extern ClientWorkerThreadList *clientWorkerThreads;



ClientWorkerThreadList *clientCreateWorkers(int nWorker);
int clientStartWorkers(ClientWorkerThreadList *workers);
ClientWorkerThread* clientFindWorker(ClientWorkerThreadList *workers);

#endif//_AppsvrClientH_
