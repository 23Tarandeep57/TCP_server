#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define IP "142.251.150.119" //IP of www.google.com
#define PORT 80 //HTTP


int main() {
	int s;
	struct sockaddr_in sock;
	char buffer[512];
	char* data;

	data = "HEAD / HTTP/1.0\n\n";

	s = socket(AF_INET, SOCK_STREAM, 0);

	if (s < 0) {
		printf("ERror No\n", errno);
		return -1;
	}
	sock.sin_addr.s_addr = inet_addr(IP);
	sock.sin_port = htons(PORT);
	sock.sin_family = AF_INET;

	if (connect(s, (struct sockaddr*) &sock, sizeof(struct sockaddr)) != 0) {
		printf("connect() ERROR\n");
		close(s);
		return -1;
	}

	write(s, data, strlen(data));
	read(s, buffer, 511);
	close(s);

	printf("\n %s \n", buffer);
	return 0;
	
}


