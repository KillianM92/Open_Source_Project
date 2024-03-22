all: server client

server: server.c
	gcc server.c -o server -Wall -Wextra

client: client.c
	gcc client.c -o client -Wall -Wextra

tar: server.c client1.c cmakefile
	tar -cf Project_Open_Source.tar server.c client.c makefile

clean:
	rm -f server client