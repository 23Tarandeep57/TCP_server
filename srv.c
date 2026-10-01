#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define PORT 8181

int main() {
	int s, c;
	socklen_t addr_len;
	struct sockaddr_in srv, cli;
	addr_len = 0;
	char buffer[512];
	char* data;

	memset(&srv, 0, sizeof(srv));
	memset(&cli, 0, sizeof(cli));

	s = socket(AF_INET, SOCK_STREAM, 0);

	if (s < 0) {
		printf("SOCKET Error: %d\n", errno);
		return -1;
	}

	srv.sin_family = AF_INET;
	srv.sin_addr.s_addr = 0;
	srv.sin_port = htons(PORT);

	if (bind(s, (struct sockaddr*)&srv, sizeof(srv)) != 0) {
		printf("BIND Error: %d\n", errno);
		close(s);
		return -1;
	}

	if (listen(s, 5) != 0) {
		printf("listen error: %d\n", errno);
		close(s);
		return -1;
	}
	
	printf("LIsetning to the 0.0.0.0:%d\n", PORT);

	c = accept(s, (struct sockaddr*) &srv, &addr_len);
	
	if (c < 0) {
		printf("Accept failed: %d\n", errno);
		close(s);
		return -1;
	}
	
	printf("Client connected!!\n");
	read(c, &buffer, 511);
	data = "yo hola amigos\n";
	write(c, data, strlen(data));

	close(s);
	close(c);

	return 0;
}
