#ifndef _AppsvrSockH_
#define _AppsvrSockH_

#define SOCK_ERR_CREATE		-1

SOCKET sockTcpServerCreate(int port, int listenqueue);
SOCKET sockTcpServerAccept(SOCKET sock, char *from_ip, unsigned int *from_port);

SOCKET sockTcpClientCreate();
int sockTcpClientConnect(SOCKET sock, char *host, short port);

int sockTcpWrite(SOCKET sock, void *buffer, int length);

SOCKET sockUdpCreate(int listenPort);
int sockUdpWrite(SOCKET sock, void *buffer, int length, char *ip, int port);
int sockUdpRead(SOCKET sock, void *buffer, int buflen, char *from_ip, unsigned int *from_port);

void sockClose(SOCKET sock);

#endif//_AppsvrSockH_

