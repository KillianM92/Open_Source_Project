#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <semaphore.h>

#define CONFIG_FILE "demon.conf"
#define PIPE_NAME "/tmp/demon_pipe"

struct demon_config {
    int min_thread;
    int max_thread;
    int max_connect_per_thread;
    int shm_size;
};

struct demon_config read_config() {
    struct demon_config config = {0};
    FILE *file = fopen(CONFIG_FILE, "r");
    if (file == NULL) {
        perror("Erreur lors de l'ouverture du fichier de configuration");
        exit(EXIT_FAILURE);
    }
    fscanf(file, "MIN_THREAD=%d\nMAX_THREAD=%d\nMAX_CONNECT_PER_THREAD=%d\nSHM_SIZE=%d\n",
           &config.min_thread, &config.max_thread, &config.max_connect_per_thread, &config.shm_size);
    fclose(file);
    return config;
}
char received_shm_name[256]; // Variable globale pour stocker le nom de la SHM reçu

sem_t* choice_sem = NULL;
sem_t* reponse_sem = NULL;

// Envoie une demande de connexion au serveur via le tube FIFO
void request_connection() {
    struct demon_config config = read_config();

    int pipe_fd, response_pipe_fd;
    char buffer[config.shm_size], response_pipe_name[256], shm_name[256];
    int pid = getpid();

    // Avant d'écrire le choix dans la SHM
    choice_sem = sem_open("/choice_sem_demon", O_CREAT, 0666, 0);
    if (choice_sem == SEM_FAILED) {
        perror("Client: sem_open failed");
        exit(EXIT_FAILURE);
    }

    // Avant de lire la réponse dans la SHM
    reponse_sem = sem_open("/reponse_sem_demon", O_CREAT, 0666, 0);
    if (reponse_sem == SEM_FAILED) {
        perror("Client: sem_open failed");
        exit(EXIT_FAILURE);
    }

    // Préparer le nom du tube de réponse basé sur le PID du client
    snprintf(response_pipe_name, sizeof(response_pipe_name), "/tmp/response_pipe_%d", pid);
    snprintf(buffer, sizeof(buffer), "SYNC:%d", pid);

    // Créer le tube de réponse avant d'envoyer la demande de connexion
    mkfifo(response_pipe_name, 0666);

    // Envoyer la demande de connexion (PID du client)
    pipe_fd = open(PIPE_NAME, O_WRONLY);
    if (pipe_fd == -1) {
        perror("Opening demon pipe failed");
        exit(EXIT_FAILURE);
    }
    if (write(pipe_fd, buffer, strlen(buffer) + 1) == -1) {
        perror("Writing to demon pipe failed");
        close(pipe_fd);
        exit(EXIT_FAILURE);
    }

    printf("Request sent : %s\n", buffer);

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

int printMenu(){
    int choice;
    printf("\nMenu :\n");
    printf("1. Afficher la date\n");
    printf("2. Afficher l'heure\n");
    printf("3. Afficher l'id de l'utilisateur\n");
    printf("4. Afficher le répertoire actuel de travail (pwd)\n");;
    printf("5. Fermer le thread actuel côté serveur\n");
    printf("6. Fermer le client\n");
    printf("Entrez votre choix : ");
    scanf("%d", &choice);
    return choice;
}

void write_choice_to_shm(int choice) {

    int shm_fd = shm_open(received_shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }

    char* shm_ptr = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        exit(EXIT_FAILURE);
    }  

    // Écrire le choix dans la SHM
    memcpy(shm_ptr, &choice, sizeof(int));

    // Signaler (post) le sémaphore une fois que le choix est écrit
    sem_post(choice_sem);

    // Libération des ressources
    munmap(shm_ptr, sizeof(int));
    close(shm_fd);
}

// Se connecte à la SHM spécifiée par le serveur et communique via cette SHM
void communicate_with_server() {
    struct demon_config config = read_config();

    int shm_fd;
    void* shm_ptr;

    // Ouvrir la SHM avec le nom reçu
    shm_fd = shm_open(received_shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }

    // Mapper la SHM dans l'espace d'adressage du processus
    shm_ptr = mmap(NULL, config.shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        exit(EXIT_FAILURE);
    }

    sem_wait(reponse_sem);

    printf("Connecté à la SHM. Lecture des données...\n");

    // Lire les données de la SHM
    printf("Données reçues du serveur : \"%s\"\n", (char*)shm_ptr);

    // Nettoyage
    munmap(shm_ptr, config.shm_size);
    close(shm_fd);
}

int main() {

    int choice;

    request_connection();

    do {
        choice = printMenu();
        
        if (choice >= 1 && choice <= 5) {
            write_choice_to_shm(choice); // Envoyer le choix au serveur via la SHM
            communicate_with_server(); // Attendre et afficher la réponse du serveur
        }
    } while (choice != 6); // Fermeture du client si l'utilisateur choisit l'option 6
    sem_close(choice_sem);
    sem_close(reponse_sem);
    sem_unlink("/choice_sem_demon");
    sem_unlink("/reponse_sem_demon");

    return 0;
}