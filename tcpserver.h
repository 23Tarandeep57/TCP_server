#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>

#define MAX_CLIENTS  128
#define MAX_EVENTS   128

#define IBUF_SZ  8192
#define OBUF_SZ  65536

#define UNAME_LEN 32
#define ROOM_LEN  32

typedef unsigned char int8;
typedef unsigned short int int16;
typedef unsigned int int32;
typedef unsigned long long int int64;

#define public __attribute__((visibility("default")))
#define private static
#define init   __attribute__((constructor));

#define reterr(x) do{ \
	server *_srv; \
	int16 _sz, _len; \
	_sz = sizeof(struct s_server) + (_len=(int16)strlen((char*)x)) + 1; \
	_srv = (server *)malloc((int)_sz); \
	assert(_srv); \
	memset((char*) _srv, 0, (int)_sz); \
	_srv->status = Errstatus; \
	memcpy((char*)_srv->errmsg, (char*)(x), (int)_len); \
	return _srv; \
}while(false);

struct s_connection {
	int32 fd;
	int8 host[16];
	int16 port;
	int (*recv)(struct s_connection *, char *, int);
    int (*send)(struct s_connection *, char *, int);
};
typedef struct s_connection connection;

 // Callback is called whenever a client has readable data.
typedef void (*callback)(connection*);

enum {
	Errstatus,
	Okstatus
};

struct s_server {
	int32 fd;
	int8 status:1;
	int16 port;
	bool (*ok)(void);
	char* (*err)(void);
	int (*listen)(void);
	callback cb;
    char errmsg[];
};
typedef struct s_server server;

int main();
private init void start(void);
private void handler_(connection*);
public server *tcpserver(int16, callback);
private bool okfunc(void);
private char *errfunc(void);
private int listenfunc(void);
private int recvfunc(connection*, char*, int);
private int sendfunc(connection*, char*, int);
