#include "tcpserver.h"


public server *globalsrv;
public connection  *globalconn;
#define handler() &handler_
#define PORT 8181

private void handler_(connection *conn) {
	char buff[128];
	memset(buff, 0, 128);
	conn->send("welcome to server\n", 19);
	conn->recv(buff, 127);
	printf("Recieved '%s'\n", buff);
	return;
}

private init void start() {
	globalsrv = (server*) 0;
	globalconn = (connection*) 0;
	return;
}

public server* tcpserver(int16 port, callback cb) {
	signed int ret;
	int32 s;
	int16 size;
	struct sockaddr_in sock;
	server* srv;

	if (globalsrv)
		reterr((char*) "TCP server already initialised");
	
	ret = socket(AF_INET, SOCK_STREAM, 0);
	if (ret < 0) {
		reterr((char*)"Failed to initialise socket");
	} else { 
		s = (int32) ret;
	}

	sock.sin_family = AF_INET;
	sock.sin_port = htons(port);
	sock.sin_addr.s_addr = INADDR_ANY;

	ret = bind((int)s, (struct sockaddr*)& sock, sizeof(struct sockaddr));
	if (ret) {
		close(s);
		reterr((char*) "Unable to bind to port");
	}


	ret = listen(s, 10);

	if (ret) {
		close(s);
		reterr((char*) "Unable to listen");
	}

	size = sizeof(server);
	srv = (server *)malloc((int) size);
	assert(srv);
	memset((char*) srv, 0, (int) size);
	

	srv->fd =s;
	srv->port = port;
	srv->status= Okstatus;
	srv->ok = &okfunc;
	srv->cb = cb;
	srv->err = &errfunc;
	srv->listen = &listenfunc;
	globalsrv = srv;
	
	return srv;
}

private bool okfunc() {
	if (!globalsrv)
		return false;
	return (globalsrv->status == Okstatus) ?
		true: false;
}

private char *errfunc() {
	if (!globalsrv) 
		return "tcp server not initialized";
	
	else if (globalsrv->status != Errstatus)
		return "no error";

	else 
		return (char*) globalsrv->errmsg;
}

private int listenfunc() {
	if (!globalsrv || globalsrv->status == Errstatus) 
		return -1;
	
	int32 s;
	signed int ret;
	struct sockaddr_in sock;
	socklen_t len;
	int16 size;
	connection *conn;

	if (fork()) 
		exit(0);

	len = sizeof (struct sockaddr_in);
	size = sizeof(connection);

	while(true) {
		ret = accept((int)globalsrv->fd, (struct sockaddr*)&sock, &len);
		if (ret < 0) {
			sleep(2);
			continue;
		}
		else
			s = (int32)ret;

		conn = (connection*) malloc((int)size);

		if (!conn) {
			close(s);
			sleep(2);
			continue;
		}
		memset((char*)conn, 0, size);
		
		conn->fd = s;
		strncpy((char*)conn->host,inet_ntoa(sock.sin_addr), 15);
		conn->port = htons(sock.sin_port);
		conn->send = &sendfunc;
		conn->recv = &recvfunc;
		if (fork()) {
			close(s);
			if (conn) 
				free(conn);
			continue;
		}
		globalconn = conn;
		globalsrv->cb(conn);
		close(s);
		exit(0);
	}
	exit(0);
	return 0;
}

private int recvfunc(char* buff, int size) {
	if (!globalconn || !buff || !size) 
		return -1;

	return read(globalconn->fd, buff, size);
}

private int sendfunc(char* buff, int size) {
	if (!globalconn || !buff || !size) 
		return -1;

	return write(globalconn->fd, buff, size);
}

int main() {
	server *serv;

	serv = tcpserver(PORT, handler());
	if (serv && !serv->ok()) {
		printf("Error %s\n", serv->err());
		return -1;
	}

	printf("Listening to port %d\n", PORT);

	return serv->listen();
}
