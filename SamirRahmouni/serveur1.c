#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <string.h>
#include <stdbool.h>

#define FIFO_PATH "daemon_fifo"
#define CONFIG_FILE "demon.conf"

struct config {
    int min_thread;
    int max_thread;
    int max_connect_per_thread;
    int shm_size;
} cfg;

struct thread_info {
    pthread_t thread_id;
    int shm_id;
    int connections_handled;
    bool is_active;
};

struct thread_info *threads;

void read_config(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("Erreur lors de l'ouverture du fichier de configuration");
        exit(EXIT_FAILURE);
    }
    fscanf(file, "MIN_THREAD = %d\nMAX_THREAD = %d\nMAX_CONNECT_PER_THREAD = %d\nSHM_SIZE = %d",
           &cfg.min_thread, &cfg.max_thread, &cfg.max_connect_per_thread, &cfg.shm_size);
    fclose(file);

    if (cfg.min_thread > cfg.max_thread) {
        fprintf(stderr, "Erreur de configuration: MIN_THREAD > MAX_THREAD\n");
        exit(EXIT_FAILURE);
    }
}

void* handle_connection(void* arg) {
    struct thread_info* info = (struct thread_info*)arg;
    info->is_active = true;

    char* shm_ptr = shmat(info->shm_id, NULL, 0);
    if (shm_ptr == (char*)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    while (info->connections_handled < cfg.max_connect_per_thread || cfg.max_connect_per_thread == 0) {
        if (strcmp(shm_ptr, "END") == 0) {
            strcpy(shm_ptr, ""); // Clear SHM
            info->connections_handled++;
            if (cfg.max_connect_per_thread != 0 && info->connections_handled >= cfg.max_connect_per_thread) {
                break; // End this thread if max connections reached
            }
        } else if (strcmp(shm_ptr, "") != 0) {
            char command[1024];
            strcpy(command, shm_ptr);
            strcpy(shm_ptr, ""); // Clear SHM

            // Execute command
            int pipefd[2];
            pipe(pipefd);
            if (fork() == 0) {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                execlp("sh", "sh", "-c", command, NULL);
                exit(EXIT_SUCCESS);
            }
            close(pipefd[1]);

            wait(NULL); // Wait for command to finish

            // Read command output
            read(pipefd[0], shm_ptr, cfg.shm_size);
            close(pipefd[0]);
        }
    }

    shmdt(shm_ptr);
    info->is_active = false;
    return NULL;
}

int main() {
    read_config(CONFIG_FILE);

    threads = malloc(cfg.max_thread * sizeof(struct thread_info));
    for (int i = 0; i < cfg.min_thread; i++) {
        threads[i].shm_id = shmget(IPC_PRIVATE, cfg.shm_size, IPC_CREAT | 0666);
        pthread_create(&threads[i].thread_id, NULL, handle_connection, &threads[i]);
    }

    mkfifo(FIFO_PATH, 0666);
    int fifo_fd = open(FIFO_PATH, O_RDONLY);
    if (fifo_fd == -1) {
        perror("Erreur lors de l'ouverture du FIFO");
        exit(EXIT_FAILURE);
    }

    char buffer[1024];
    while (read(fifo_fd, buffer, sizeof(buffer)) > 0) {
        // Logic to handle new connections (to be implemented)
    }

    for (int i = 0; i < cfg.max_thread; i++) {
        if (threads[i].is_active) {
            pthread_join(threads[i].thread_id, NULL);
            shmctl(threads[i].shm_id, IPC_RMID, NULL); // Remove SHM segment
        }
    }

    free(threads);
    close(fifo_fd);
    unlink(FIFO_PATH);

    return 0;
}