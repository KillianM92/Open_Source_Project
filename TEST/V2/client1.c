
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>

#define CONFIG_FILE "demon.conf"
#define PIPE_NAME "/tmp/demon_pipe"
#define BUFFER_SIZE 1024

char received_shm_name[BUFFER_SIZE]; // Variable globale pour stocker le nom de la SHM reçu

// Envoie une demande de connexion au serveur via le tube FIFO
void request_connection() {
    int pipe_fd, response_pipe_fd;
    char buffer[BUFFER_SIZE], response_pipe_name[256], shm_name[256];
    int pid = getpid();

    // Préparer le nom du tube de réponse basé sur le PID du client
    snprintf(response_pipe_name, sizeof(response_pipe_name), "/tmp/response_pipe_%d", pid);
    snprintf(buffer, sizeof(buffer), "%d", pid);

    // Créer le tube de réponse avant d'envoyer la demande de connexion
    mkfifo(response_pipe_name, 0666);

    // Envoyer la demande de connexion (PID du client)
    pipe_fd = open(PIPE_NAME, O_WRONLY);
    if (pipe_fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }
    if (write(pipe_fd, buffer, strlen(buffer) + 1) == -1) {
        perror("write");
        close(pipe_fd);
        exit(EXIT_FAILURE);
    }
    close(pipe_fd);

    // Ouvrir le tube de réponse pour lire le nom de la SHM
    response_pipe_fd = open(response_pipe_name, O_RDONLY);
    if (response_pipe_fd == -1) {
        perror("open response pipe");
        exit(EXIT_FAILURE);
    }
    if (read(response_pipe_fd, shm_name, sizeof(shm_name)) == -1) {
        perror("read from response pipe");
        close(response_pipe_fd);
        exit(EXIT_FAILURE);
    }
    close(response_pipe_fd);
    unlink(response_pipe_name); // Nettoyer le tube de réponse après l'utilisation
    
    // Stocker le nom de la SHM reçu dans la variable globale
    strncpy(received_shm_name, shm_name, sizeof(received_shm_name));
    printf("Nom de la SHM reçu : %s\n", received_shm_name);
}

// Se connecte à la SHM spécifiée par le serveur et communique via cette SHM
void communicate_with_server() {
    int shm_fd;
    void* shm_ptr;

    // Ouvrir la SHM avec le nom reçu
    shm_fd = shm_open(received_shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }

    // Mapper la SHM dans l'espace d'adressage du processus
    shm_ptr = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        exit(EXIT_FAILURE);
    }

    printf("Connecté à la SHM. Lecture des données...\n");

    // Lire les données de la SHM
    printf("Données reçues du serveur : \"%s\"\n", (char*)shm_ptr);

    // Nettoyage
    munmap(shm_ptr, BUFFER_SIZE);
    close(shm_fd);
}

int main() {
    request_connection();
    sleep(10); // Attente simpliste pour la synchronisation avec le serveur, à ajuster selon votre logique d'application
    communicate_with_server();

    return 0;
}