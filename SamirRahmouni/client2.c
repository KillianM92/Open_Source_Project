#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <string.h>

#define FIFO_PATH "daemon_fifo"

int main() {
    int fifo_fd = open(FIFO_PATH, O_WRONLY);
    if (fifo_fd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    // Envoi de la demande de connexion
    char* message = "SYNC";
    write(fifo_fd, message, strlen(message) + 1);
    close(fifo_fd);

    // Ici, le client devrait recevoir le shm_id du démon, pour cet exemple, nous allons le simuler
    int shm_id; // Ce devrait être reçu du démon
    printf("Entrez l'ID de SHM reçu du démon: ");
    scanf("%d", &shm_id);

    char* shm_ptr = shmat(shm_id, NULL, 0);
    if (shm_ptr == (char*)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    // Envoi d'une requête au démon via SHM
    printf("Entrez votre commande: ");
    scanf("%s", shm_ptr);

    // Attente de la réponse
    while (strcmp(shm_ptr, "") == 0) {
        sleep(1); // Attente active, à améliorer
    }
    printf("Résultat de la commande: %s\n", shm_ptr);

    // Nettoyage
    strcpy(shm_ptr, "END"); // Signale la fin de la connexion
    shmdt(shm_ptr);

    return 0;
}