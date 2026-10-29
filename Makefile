flags := -O2 -Wall -I.

all:tcpserver chat-client

tcpserver: tcpserver.o message.o
	gcc $(flags) tcpserver.o message.o -lcjson -o tcpserver
tcpserver.o: tcpserver.c
	gcc $(flags) -c tcpserver.c
message.o: message.c
	gcc $(flags) -c message.c

chat-client: client.o
	gcc $(flags) client.o -pthread -o chat-client
client.o: client.c
	gcc $(flags) -c client.c

clean:
	rm -f *.o tcpserver chat-client
