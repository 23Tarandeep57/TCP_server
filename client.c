#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

struct client_state {
	int fd;
	volatile bool running;
};

static void *receive_messages(void *arg) {
	struct client_state *state = arg;
	char buffer[4096];
	ssize_t n;

	while (state->running && (n = read(state->fd, buffer, sizeof(buffer) - 1)) > 0) {
		buffer[n] = '\0';
		fputs(buffer, stdout);
		fflush(stdout);
	}

	state->running = false;
	return NULL;
}

static int connect_to_server(const char *host, const char *port) {
	struct addrinfo hints = {0};
	struct addrinfo *results;
	struct addrinfo *entry;
	int fd = -1;

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, port, &hints, &results) != 0)
		return -1;

	for (entry = results; entry; entry = entry->ai_next) {
		fd = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, entry->ai_addr, entry->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}

	freeaddrinfo(results);
	return fd;
}

static bool send_line(int fd, const char *line) {
	size_t len = strlen(line);
	size_t sent = 0;

	while (sent < len) {
		ssize_t n = write(fd, line + sent, len - sent);
		if (n > 0) {
			sent += (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		return false;
	}
	return true;
}

static int start_tmux(const char *program, const char *host,
		      const char *port, const char *username) {
	char session[128];
	int n = snprintf(session, sizeof(session), "tcp-chat-%s", username);
	if (n < 0 || (size_t)n >= sizeof(session))
		return -1;

	execlp("tmux", "tmux", "new-session", "-A", "-s", session,
	       program, host, port, username, (char *)NULL);
	return -1;
}

static bool open_chat_pane(const char *program, const char *host,
			   const char *port, const char *username,
			   const char *chat) {
	pid_t pid = fork();
	if (pid < 0)
		return false;
	if (pid == 0) {
		execlp("tmux", "tmux", "split-window", "-h",
		       program, host, port, username, chat, (char *)NULL);
		_exit(127);
	}

	int status;
	if (waitpid(pid, &status, 0) < 0)
		return false;
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int main(int argc, char **argv) {
	const char *host = argc > 1 ? argv[1] : "127.0.0.1";
	const char *port = argc > 2 ? argv[2] : "8181";
	const char *username = argc > 3 ? argv[3] : NULL;
	const char *initial_chat = argc > 4 ? argv[4] : NULL;
	char line[4096];
	struct client_state state;
	pthread_t receiver;

	if (!username) {
		fprintf(stderr, "usage: %s [host] [port] <username> [chat]\n", argv[0]);
		return 1;
	}

	if (!getenv("TMUX"))
		start_tmux(argv[0], host, port, username);

	state.fd = connect_to_server(host, port);
	state.running = state.fd >= 0;
	if (!state.running) {
		perror("connect");
		return 1;
	}

	if (!send_line(state.fd, username) || !send_line(state.fd, "\n")) {
		close(state.fd);
		return 1;
	}

	if (pthread_create(&receiver, NULL, receive_messages, &state) != 0) {
		close(state.fd);
		return 1;
	}

	if (initial_chat) {
		char command[128];
		int n = snprintf(command, sizeof(command), "CD %s\n", initial_chat);
		if (n < 0 || (size_t)n >= sizeof(command) ||
		    !send_line(state.fd, command)) {
			state.running = false;
		}
	}

	printf("Commands: /chat <user>, /quit, /ls, /exit\n");
	while (state.running && fgets(line, sizeof(line), stdin)) {
		if (strncmp(line, "/chat ", 6) == 0) {
			char chat[128];
			if (sscanf(line + 6, "%127s", chat) == 1) {
				if (!open_chat_pane(argv[0], host, port, username, chat))
					fprintf(stderr, "failed to open chat pane\n");
				continue;
			}
		} else if (strcmp(line, "/quit\n") == 0) {
			if (!send_line(state.fd, "QUIT\n"))
				break;
			break;
		} else if (strcmp(line, "/ls\n") == 0) {
			memcpy(line, "LS\n", 4);
		} else if (strcmp(line, "/exit\n") == 0) {
			break;
		}

		if (!send_line(state.fd, line))
			break;
	}

	state.running = false;
	shutdown(state.fd, SHUT_RDWR);
	close(state.fd);
	pthread_join(receiver, NULL);
	return 0;
}
