#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define SHMSZ 1024

int main() {
    key_t key;
    int shmid;
    char *shm, *s;

    // Connexion à la zone de mémoire partagée
    if ((key = ftok(".", 'R')) == -1) {
        perror("ftok");
        exit(1);
    }

    if ((shmid = shmget(key, SHMSZ, 0666)) < 0) {
        perror("shmget");
        exit(1);
    }

    if ((shm = shmat(shmid, NULL, 0)) == (char *) -1) {
        perror("shmat");
        exit(1);
    }

    // Code pour envoyer une requête au serveur ici

    exit(0);
}