flags := -O2 -Wall -I.

all:tcpserver

tcpserver: tcpserver.o
	gcc $(flags) tcpserver.o -o tcpserver
tcpserver.o: tcpserver.c
	gcc $(flags) -c tcpserver.c

clean:
	rm -f *.o tcpserver
