#include "tcpserver.h"

public server *globalsrv;
// Each epoll event carries its own connection pointer.
#define handler() &handler_
#define PORT 8181

char* all_clients[]= {"max", "taran", "kola"};
int num_clients = 3;



static bool authenticate(const char* name) {// TODO: Passworrd
	for (int i = 0; i < num_clients; i++) {
		if (strcmp(name, all_clients[i]) == 0)
			return true;
	}
	
	return false;
}

//handler_ only deals with a particular connection
private void handler_(connection *conn) {
	char buff[128];
	memset(buff, 0, sizeof(buff));
	//conn->send(conn, "welcome to server\n", 19);

    /* Because the socket is non-blocking and we are using
     * edge-triggered epoll, keep reading until EAGAIN*/
	while (1) {
		int ret = conn->recv(conn, buff, sizeof(buff) - 1);

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

		for (int i = 0; i < ret; i++) {
			char ch = buff[i];

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
				if (authenticate(conn->input)) {
					strncpy(conn->username, conn->input, sizeof(conn->username) - 1);
					conn->auth = AUTHENTICATED;

					printf("Authenticated: %s\n", conn->username);
					conn->send(conn, "Login successful!\n", 18);
				} else {
					conn->send(conn, "Unknown username.\n", 19);
					conn->send(conn, "Enter your username: ", 21);
				}
			} else {
				printf("[%s]: %s\n", conn->username, conn->input);

				// TODO: interpret LS, CD, SEND, etc.
			}
		}
	}
}

private init void start() {
	globalsrv = (server*) 0;
	return;
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
		
		if (conn->send(conn, "Enter your username: ", 21) < 0) {
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

private int recvfunc(connection *conn, char* buff, int size) {
	if (!conn || !buff || !size)
		return -1;

	return read(conn->fd, buff, size);
}

private int sendfunc(connection *conn, char* buff, int size) {
	if (!conn || !buff || !size)
		return -1;

	return write(conn->fd, buff, size);
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
