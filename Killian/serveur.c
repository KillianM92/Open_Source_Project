#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#define CONFIG_FILE "demon.conf"
#define PIPE_NAME "/tmp/demon_pipe"
#define BUFFER_SIZE 1024

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

void* handle_client(void* arg) {
    int client_pid = *(int*)arg;
    free(arg); // Libérer la mémoire allouée pour le PID du client

    char shm_name[256], response_pipe_name[256];
    snprintf(shm_name, sizeof(shm_name), "/demon_shm_%d", client_pid);
    snprintf(response_pipe_name, sizeof(response_pipe_name), "/tmp/response_pipe_%d", client_pid);

    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        pthread_exit(NULL);
    }
    // Supposons que BUFFER_SIZE est la taille désirée pour la SHM
    if (ftruncate(shm_fd, BUFFER_SIZE) == -1) {
        perror("ftruncate");
        close(shm_fd);
        pthread_exit(NULL);
    }

    void* shm_ptr = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        pthread_exit(NULL);
    }

    int response_pipe_fd = open(response_pipe_name, O_WRONLY);
    if (response_pipe_fd == -1) {
        perror("open response pipe");
        munmap(shm_ptr, BUFFER_SIZE);
        close(shm_fd);
        pthread_exit(NULL);
    }

    if (write(response_pipe_fd, shm_name, strlen(shm_name) + 1) == -1) {
        perror("write to response pipe");
    }

    close(response_pipe_fd);

    const char* message = "Hello from server";
    memcpy(shm_ptr, message, strlen(message) + 1);

    sleep(5); // Modifiez selon les besoins de votre application

    munmap(shm_ptr, BUFFER_SIZE);
    close(shm_fd);

    printf("Traitement du client (PID: %d) terminé.\n", client_pid);
    pthread_exit(NULL);
    shm_unlink(shm_name);
}

void startdemon() {
    struct demon_config config = read_config();
    
    printf("Configuration lue : MIN_THREAD=%d, MAX_THREAD=%d, MAX_CONNECT_PER_THREAD=%d, SHM_SIZE=%d\n",
           config.min_thread, config.max_thread, config.max_connect_per_thread, config.shm_size);

    printf("Serveur en attente de demandes de connexion...\n");
    // Tentative de création du tube FIFO
    if (mkfifo(PIPE_NAME, 0666) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo");
            exit(EXIT_FAILURE);
        }
    }

    // Tentative d'ouverture du tube FIFO
    int pipe_fd = open(PIPE_NAME, O_RDONLY | O_NONBLOCK);
    if (pipe_fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    char buffer[BUFFER_SIZE];
    while (1) {
        ssize_t read_bytes = read(pipe_fd, buffer, BUFFER_SIZE - 1);
        if (read_bytes > 0) {
            buffer[read_bytes] = '\0';
            int* client_pid = malloc(sizeof(int));
            sscanf(buffer, "%d", client_pid);
            printf("Demande reçue: %d\n", *client_pid);

            pthread_t thread_id;
            if (pthread_create(&thread_id, NULL, handle_client, (void*)client_pid) != 0) {
                perror("pthread_create");
                exit(EXIT_FAILURE);
            }
            pthread_detach(thread_id);
        } else if (read_bytes == 0) {
            close(pipe_fd);
            pipe_fd = open(PIPE_NAME, O_RDONLY);
            if (pipe_fd == -1) {
                perror("open");
                exit(EXIT_FAILURE);
            }
        } else {
            perror("read");
            exit(EXIT_FAILURE);
        }
    }
}

int main() {
    struct demon_config config = read_config();
    
    printf("Serveur prêt. Configuration : MIN_THREAD=%d, MAX_THREAD=%d, MAX_CONNECT_PER_THREAD=%d, SHM_SIZE=%d\n",
           config.min_thread, config.max_thread, config.max_connect_per_thread, config.shm_size);

    startdemon(); // Commencer à écouter les demandes de connexion
    return 0; // Cette ligne ne sera jamais atteinte dans l'implémentation actuelle
}