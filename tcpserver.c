#include "tcpserver.h"

public server *globalsrv;
// Each epoll event carries its own connection pointer.
#define handler() &handler_
#define PORT 8181

private init void start() {
	globalsrv = (server*) 0;
	return;
}

int name_to_fd(char* name) {
	for (int i = 0; i < globalsrv->num_clients; i++) {
		if (strcmp(globalsrv->creds[i].username, name) == 0) 
			return globalsrv->creds[i].fd;
	}
	return 0;
}
char* fd_to_name(int fd) {
	for (int i= 0; i < globalsrv->num_clients; i++) {
		if (fd == globalsrv->creds[i].fd) 
			return globalsrv->creds[i].username;
	}
	return NULL;
}

static char **parser(char *instr) {
    static char *parts[100];
    char *saveptr = NULL;
    char *token;
    int n = 0;

    memset(parts, 0, sizeof(parts));

    token = strtok_r(instr, " \t", &saveptr);

    while (token != NULL && n < 99) {
        parts[n++] = token;
        token = strtok_r(NULL, " \t", &saveptr);
    }

    parts[n] = NULL;
    return parts;
} 

void netio(connection* conn) {
	int i;
	char* name;
	char** parts = parser(conn->input);
	if (conn->target_fd != -1) {
		conn->send(conn->target_fd, conn->username, sizeof(conn->username));
		conn->send(conn->target_fd, conn->input, sizeof(conn->input));
	}
	if (strcmp(conn->input, "LS") == 0) {
    	for (i = 0; i < globalsrv->num_clients; i++) {
        	name = globalsrv->creds[i].username;
        	conn->send(conn->fd, name, strlen(name));
        	conn->send(conn->fd, "  ", 2);
    	}
    	conn->send(conn->fd, "\n", 1);
	}else if (strcmp(parts[0], "CD") == 0) {//TODO assert parts.size == 2
		strcat(conn->loc, parts[1]);
		int target_fd = name_to_fd(parts[1]);
		if (target_fd == 0) {
			printf("NO USER OF THIS NAME");
		}
		conn->target_fd = target_fd;
	}
	else {
		printf("[%s]: %s\n", conn->username, conn->input);
	}
}

static bool authenticate(const char *name, int fd) {
    for (int i = 0; i < globalsrv->num_clients; i++) {
        if (strcmp(name, globalsrv->creds[i].username) == 0) {
            globalsrv->creds[i].fd = fd;
            return true;
        }
    }

    return false;
}

//handler_ only deals with a particular connection
private void handler_(connection *conn) {
	char buff[OBUF_SZ];
	int ret, i;
	char ch;
	memset(buff, 0, sizeof(buff));
	//conn->send(conn->fd, "welcome to server\n", 19);

    /* Because the socket is non-blocking and we are using
     * edge-triggered epoll, keep reading until EAGAIN*/
	while (1) {
		ret = conn->recv(conn->fd, buff, sizeof(buff) - 1);

		if (ret == 0) {
			close(conn->fd);
			free(conn);
			return;
		} else if (ret < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK) 
				break;

			perror("recv");
			close(conn->fd);
			free(conn);
			return;
		}

		for (i = 0; i < ret; i++) {
			ch = buff[i];

			if (ch == '\r') continue;
			if (ch != '\n') {
				if (conn->input_len >= IBUF_SZ - 1) {
					close(conn->fd);
					free(conn);
					return;
				}

				conn->input[conn->input_len++] = ch;
				continue;
			}

			// A complete line has arrived.
			conn->input[conn->input_len] = '\0';
			conn->input_len = 0;

			if (conn->auth == AUTH_PENDING) {
				if (authenticate(conn->input, conn->fd)) {
					strncpy(conn->username, conn->input, sizeof(conn->username) - 1);
					conn->auth = AUTHENTICATED;

					printf("Authenticated: %s\n", conn->username);
					conn->send(conn->fd, "You can join the Counsil ;)\n", 28);
				} else {
					conn->send(conn->fd, "I don't know you. 0_o \n", 24);
					conn->send(conn->fd, "State your name -_-: ", 21);
				}
			} else {
					netio(conn);
				// TODO: interpret LS, CD, SEND, etc.
			}
		}
	}
}

public server* tcpserver(int16 port, callback cb) {
	signed int ret;
	int32 s;
	int16 size;
	struct sockaddr_in sock;
	server* srv;

	if (globalsrv){
		reterr((char*) "TCP server already initialised");
	}
	
	ret = socket(AF_INET, SOCK_STREAM, 0);
	if (ret < 0) {
		reterr((char*)"Failed to initialise socket");
	}
	else {
		s = (int32) ret;
	}
	
	int opt = 1;
	setsockopt(
		s,
		SOL_SOCKET,
		SO_REUSEADDR,
		&opt,
		sizeof(opt)
	);

	//Make server socket non-blocking
	fcntl(s, F_SETFL, O_NONBLOCK);

	memset(&sock, 0, sizeof(sock));

	sock.sin_family = AF_INET;
	sock.sin_port = htons(port);
	sock.sin_addr.s_addr = htonl(INADDR_ANY);

	ret = bind((int)s, (struct sockaddr*)& sock, sizeof(struct sockaddr));
	if (ret) {
		close(s);
		reterr((char*) "Unable to bind to port");
	}

	ret = listen(s, 128);

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
	srv->num_clients= 2;
	strcpy(srv->creds[0].username, "taran");
	srv->creds[0].fd = -1;
	strcpy(srv->creds[1].username, "kola");
	srv->creds[1].fd = -1;
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

private int create_epoll(void) {
	int efd = epoll_create1(0);
	if (efd == -1) {
		perror("epoll_create1");
		return -1;
	}

	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN | EPOLLET;
	ev.data.fd = globalsrv->fd;

	if (epoll_ctl(efd, EPOLL_CTL_ADD, globalsrv->fd, &ev) == -1) {
		perror("epoll_ctl: server");
		close(efd);
		return -1;
	}

	return efd;
}

private void accept_clients(int efd) {
	struct sockaddr_in sock;
	struct epoll_event ev;
	int client_fd;
	socklen_t len = sizeof(sock);
	
	while (true) {
		client_fd = accept( globalsrv->fd, (struct sockaddr *)&sock, &len);

		if (client_fd == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;

			if (errno == EINTR)
				continue;

			perror("accept");
			break;
		}

		if (fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK) == -1) {
			perror("fcntl: client");
			close(client_fd);
			continue;
		}

		connection *conn = malloc(sizeof(connection));
		if (!conn) {
			perror("malloc");
			close(client_fd);
			continue;
		}

		memset(conn, 0, sizeof(connection));
		conn->fd = client_fd;

		strncpy((char *)conn->host, inet_ntoa(sock.sin_addr), 15);
		conn->host[15] = '\0';
		conn->port = ntohs(sock.sin_port);
		conn->send = &sendfunc;
		conn->recv = &recvfunc;
		conn->auth = AUTH_PENDING;
		conn->input_len = 0;
		conn->loc = malloc(100);
		if (conn->loc == NULL) {
			close(client_fd);
			free(conn);
			continue;
		}

		strcpy(conn->loc, "$");
		conn->target_fd = -1;
		
		if (conn->send(conn->fd, "State your name: ", 18) < 0) {
			close(client_fd);
			free(conn);
			sleep(2);
			continue;
		}

		memset(&ev, 0, sizeof(ev));
		ev.events = EPOLLIN | EPOLLET;
		ev.data.ptr = conn;

		if (epoll_ctl(efd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
			perror("epoll_ctl: client");
			close(client_fd);
			free(conn);
			continue;
		}

		printf("Client connected: %s:%d fd=%d\n", conn->host, conn->port, conn->fd);
	}
}

private void handle_client_event(int efd, struct epoll_event *event ) {
	connection *conn = (connection *)event->data.ptr;

	if (event->events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
		epoll_ctl(efd, EPOLL_CTL_DEL, conn->fd, NULL);

		printf("Client disconnected: %s:%d\n", conn->host, conn->port);
		close(conn->fd);
		free(conn);
		return;
	}
	
	if (event->events & EPOLLIN)
		globalsrv->cb(conn);
}

private int listenfunc(void) {
	if (!globalsrv || globalsrv->status == Errstatus)
		return -1;

	int efd = create_epoll();
	if (efd == -1)
		return -1;

	struct epoll_event events[MAX_EVENTS];

	printf("epoll server started\n");

	while (true) {
		int nfds = epoll_wait(efd, events, MAX_EVENTS, -1);

		if (nfds == -1) {
			if (errno == EINTR)
				continue;

			perror("epoll_wait");
			break;
		}

		for (int i = 0; i < nfds; i++) {
			if (events[i].data.fd == globalsrv->fd)
				accept_clients(efd);
			else
				handle_client_event(efd, &events[i]);
		}
	}
	close(efd);
	return 0;
}

private int recvfunc(int fd, char* buff, int size) {
	if (!buff || !size)
		return -1;

	return read(fd, buff, size);
}

private int sendfunc(int fd, char* buff, int size) {
	if (!buff || !size)
		return -1;

	return write(fd, buff, size);
}

int main() {
	server *serv;

	serv = tcpserver(PORT, handler());
	if (!serv) {
		printf("Failed to create server\n");
		return -1;
	}

	if (serv->status == Errstatus) {
		printf("Error: %s\n", serv->errmsg);
		free(serv);
		return -1;
	}

	if (!serv->ok()) {
		printf("Error %s\n", serv->err());
		free(serv);
		return -1;
	}
	printf("Listening to port %d\n", PORT);

	return serv->listen();
}
