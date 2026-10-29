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
#include <cjson/cJSON.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <poll.h>
#include <sys/stat.h>

#define DATA_DIR    "data"
#define HISTORY_DIR "data/history"
#define PENDING_DIR "data/pending"
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

typedef enum {
	AUTH_PENDING,
	AUTHENTICATED
} auth_state;

struct s_cred {
	char username[UNAME_LEN];
	int fd;
	struct s_connection *session;
};
typedef struct s_cred cred;

struct s_connection {
	int32 fd;
	int8 host[16];
	int16 port;
	int (*recv)(int, char *, int);
	int (*send)(int, char *, int);

	auth_state auth;
	char input[IBUF_SZ];
	char username[UNAME_LEN];
	size_t input_len;
	char target_name[UNAME_LEN];
	int target_fd;
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
	int num_clients;
	cred creds[MAX_CLIENTS];
	bool (*ok)(void);
	char* (*err)(void);
	int (*listen)(void);
	callback cb;
	char errmsg[];
};
typedef struct s_server server;

int main();
private init void start(void);
int name_to_fd(char*);
char* fd_to_name(int);
private void mark_offline(int);
private char **parser(char*);
void netio(connection*);
int valid_user(const char* );
private bool authenticate(const char*, int fd);
private void handler_(connection*);
public server *tcpserver(int16, callback);
private bool okfunc(void);
private char *errfunc(void);
private int create_epoll(void);
private void accept_clients(int);
private void handle_client_event(int, struct epoll_event*);
private int listenfunc(void);
private int recvfunc(int, char*, int);
private int sendfunc(int, char*, int);
bool msg_init(void);
private bool make_path(char*, size_t, const char*, const char*);
private bool his_path(char*, size_t, const char*, const char*);
private bool pending_path(char*, size_t, const char*);
private int64 new_id(void);
private bool append_json(FILE *, const cJSON*);
private cJSON *make_msg(int64, const char*, const char*, const char*);

bool store_msg(const char*,const char*, const char*,int64*);
bool deliver_pending(const char *, int);
bool deliver_history(const char *, const char *, int);
private bool sendall(int, const char*, size_t);
bool deliver_one(int, int64, const char*, const char*, const char*);
bool ack_msg(const char *,int64);
