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
		if (strcmp(globalsrv->creds[i].username, name) == 0) {
			printf("%s : %s", globalsrv->creds[i].username, name);
			return globalsrv->creds[i].fd;
		}
	}
	return -1;
}
char* fd_to_name(int fd) {
	for (int i= 0; i < globalsrv->num_clients; i++) {
		if (fd == globalsrv->creds[i].fd) 
			return globalsrv->creds[i].username;
	}
	return NULL;
}
private void mark_offline(int fd) {
	int i;
	for (i = 0; i < globalsrv->num_clients; i++) {
		if (globalsrv->creds[i].fd == fd) {
			globalsrv->creds[i].fd = -1;
			globalsrv->creds[i].session = NULL;
			return;
		}
	}
}
private char **parser(char *instr) {
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
	char message[IBUF_SZ];
	int i;
	char* name;
    snprintf(message, sizeof(message), "%s", conn->input);
	
	char parsebuf[IBUF_SZ];
	snprintf(parsebuf, sizeof(parsebuf), "%s", message);

	char **parts = parser(parsebuf);
	if (parts[0] == NULL)
		return;
	
	if (strcmp(parts[0], "QUIT") == 0 && !parts[1]) {
		conn->target_fd = -1;
		conn->target_name[0] = '\0';
		return;
	}
	if (strcmp(parts[0], "LS") ==0 && !parts[1]){
    	for (i = 0; i < globalsrv->num_clients; i++) {
			char line[UNAME_LEN+32];
			snprintf(line, sizeof(line), "%s %s\n",
					globalsrv->creds[i].username,
					globalsrv->creds[i].fd >= 0 ? 
						"[online]" : "[offline]");
		conn->send(conn->fd, line, (int)strlen(line));
		}
		return;
	}
	if (strcmp(parts[0], "CD") == 0) {//TODO assert parts.size == 2
		if (!parts[1]) {
			conn->send(conn->fd, "USAGE: CD <username>\n", 21);
			return;
		}
		if (valid_user(parts[1]) == -1) {
			conn->send(conn->fd, "Unknown USER\n", 13);
			return;
		}

		snprintf(conn->target_name, sizeof(conn->target_name), "%s", parts[1]);
		conn->target_fd = name_to_fd(parts[1]);
		if (!deliver_history(conn->username, conn->target_name, conn->fd)) {
			conn->target_fd = -1;
			conn->target_name[0] = '\0';
			conn->send(conn->fd, "Chat history unavailable\n", 24);
			return;
		}
		conn->send(conn->fd, "Chat selected\n", 14);
		return;
	}
	if (strcmp(parts[0], "ACK") == 0){
		if (!parts[1]) {
			conn->send(conn->fd, "USAGE: ACK <id>\n", 16);
			return;
		}
		char *end;
		errno = 0;
		int64 raw_id= strtoull(parts[1], &end, 10);

		if(errno || end == parts[1] || *end != '\0' || raw_id== 0) {
			conn->send(conn->fd, "Invalid_messageId\n", 18);
			return;
		}

		if (ack_msg(conn->username, (int64)raw_id)) 
			conn->send(conn->fd, "ACK OK\n", 7);
		else 
			conn->send(conn->fd, "ACK FAILED\n", 11);
		return;
	} 
	if (conn->target_name[0] == '\0') {
		conn->send(conn->fd, "Select a recipient using CD first\n", 34);
		return;
	}

	int64 id;
	if (!store_msg(conn->username, conn->target_name, message, &id)) {
		conn->send(conn->fd, "FAiled to persist message\n", 26);
		return;
	}

	int recipient = valid_user(conn->target_name);
	connection *target = recipient >= 0 ? globalsrv->creds[recipient].session : NULL;
	if (target && strcmp(target->target_name, conn->username) == 0) {
		size_t sz = sizeof(conn->username) + 8;
		char msg[sz];
		snprintf(msg, sz, "[%s]:", conn->username);
		target->send(target->fd, msg, strlen(msg));
		target->send(target->fd, message, strlen(message));
		target->send(target->fd, "\n", 1);
	}

	char resp[100];
	snprintf(resp, sizeof(resp), "Persisted %" PRIu64 "\n", id);
	conn->send(conn->fd, resp, (int)strlen(resp));
}

int valid_user(const char* name) {
	for (int i = 0; i < globalsrv->num_clients; i++) {
		if (strcmp(name, globalsrv->creds[i].username) == 0) {
			return i;
		}
	}

	return -1;
}

private bool authenticate(const char *name, int fd) {
	int id;
	id =  valid_user(name);
	if (id == -1) return false;

	globalsrv->creds[id].fd = fd;	
	return true;
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
			mark_offline(conn->fd);
			close(conn->fd);
			free(conn);
			return;
		} else if (ret < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK) 
				break;

			perror("recv");
			mark_offline(conn->fd);
			close(conn->fd);
			free(conn);
			return;
		}

		for (i = 0; i < ret; i++) {
			ch = buff[i];

			if (ch == '\r') continue;
			if (ch != '\n') {
				if (conn->input_len >= IBUF_SZ - 1) {
					mark_offline(conn->fd);
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
					snprintf(conn->username, sizeof(conn->username), "%s", conn->input);
					conn->auth = AUTHENTICATED;
					globalsrv->creds[valid_user(conn->username)].session = conn;
					printf("%s\n", conn->username);
					printf("Authenticated: %s\n", conn->username);
					conn->send(conn->fd, "You can join the Counsil ;)\n", 28);
				} else {
					conn->send(conn->fd, "I don't know you. 0_o \n", 24);
					conn->send(conn->fd, "State your name -_-: ", 21);
				}
			} else {
				netio(conn);
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
		conn->target_fd = -1;
		conn->target_name[0] = '\0';
		
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
		mark_offline(conn->fd);
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
	if (!msg_init()) {
		perror("Message_init() failed");
		free(serv);
		return -1;
	}
	printf("Listening to port %d\n", PORT);
	
	return serv->listen();
}
