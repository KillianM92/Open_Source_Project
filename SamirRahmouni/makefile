all: server client1 client2

server: server.c
	gcc server1.c -o server -Wall -Wextra

client1: client1.c
	gcc client1.c -o client -Wall -Wextra

client2: client2.c
	gcc client2.c -o client -Wall -Wextra

tar: server1.c client1.c client 2.cmakefile
	tar -cf Project_Open_Source.tar server.c client1.c client2.c makefile

clean:
	rm -f server client1 client2