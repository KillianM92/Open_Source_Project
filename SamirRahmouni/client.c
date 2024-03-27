#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <unistd.h>
#include <string.h>

#define TUBE_NAME "demon_tube"

int request_connection() {
    int tube_fd = open(TUBE_NAME, O_WRONLY);
    if (tube_fd < 0) {
        perror("Failed to open tube for writing");
        exit(EXIT_FAILURE);
    }
    // Envoie une demande de synchronisation au serveur
    if (write(tube_fd, "SYNC", strlen("SYNC") + 1) == -1) {
        perror("Failed to write to tube");
        close(tube_fd);
        exit(EXIT_FAILURE);
    }
    close(tube_fd);

    // Attendre la réponse du serveur pour obtenir l'ID de la mémoire partagée (SHM_ID).
    // Cette partie doit être mise en place pour recevoir l'ID SHM du serveur.
    // Dans ce script, nous supposons que le client connaît déjà l'ID SHM par un mécanisme externe.

    // Pour cet exemple, l'utilisateur doit entrer l'ID SHM manuellement.
    int shm_id;
    printf("Please enter the SHM ID received from the server: ");
    scanf("%d", &shm_id);
    return shm_id;
}

void communicate_with_server(int shm_id) {
    char* shared_memory = (char*) shmat(shm_id, NULL, 0);
    if (shared_memory == (char*) -1) {
        perror("Failed to attach shared memory");
        exit(EXIT_FAILURE);
    }

    // Nettoyage initial de la mémoire partagée
    memset(shared_memory, 0, 1024); // Assumons SHM_SIZE = 1024 pour cet exemple

    // Envoi de la commande au serveur via la mémoire partagée
    printf("Enter your command: ");
    scanf(" %[^\n]", shared_memory);

    // Attente de la réponse du serveur
    while (strlen(shared_memory) == 0) {
        usleep(100000); // Attente active, à améliorer dans une implémentation réelle
    }

    printf("Server response:\n%s\n", shared_memory);

    // Envoi d'une commande de fin pour terminer la connexion
    strcpy(shared_memory, "END");

    // Détachement de la mémoire partagée
    shmdt(shared_memory);
}

int main() {
    int shm_id = request_connection();
    communicate_with_server(shm_id);

    return 0;
}
