all: serveur client

serveur: serveur.c
	gcc serveur.c -o serveur -Wall -pthread

client: client.c
	gcc client.c -o client -Wall

tar: server.c client.c makefile Rapport_Projet_OpenSource_Groupe_4.pdf
	tar -cf Projet_OpenSource_Groupe_4.tar server.c client.c makefile Rapport_Projet_OpenSource_Groupe_4.pdf

clean:
	rm -f serveur client Projet_OpenSource_Groupe_4.tar