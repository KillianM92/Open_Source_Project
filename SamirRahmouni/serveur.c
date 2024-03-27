#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

#define TUBE_NAME "demon_tube"
#define CONFIG_FILE "demon.conf"

typedef struct thread_info {
    pthread_t thread_id;
    int shm_id;
    int active;
    int connections_handled;
    char* shm_ptr;
} thread_info_t;

int MIN_THREAD, MAX_THREAD, MAX_CONNECT_PER_THREAD, SHM_SIZE;
pthread_mutex_t lock;
thread_info_t* thread_pool;
int total_threads_initialized = 0;

void load_config() {
    FILE* file = fopen(CONFIG_FILE, "r");
    if (!file) {
        perror("Failed to open config file");
        exit(EXIT_FAILURE);
    }
    fscanf(file, "MIN_THREAD = %d\nMAX_THREAD = %d\nMAX_CONNECT_PER_THREAD = %d\nSHM_SIZE = %d\n",
           &MIN_THREAD, &MAX_THREAD, &MAX_CONNECT_PER_THREAD, &SHM_SIZE);
    fclose(file);
}

void* handle_client(void* arg) {
    thread_info_t* info = (thread_info_t*)arg;

    while (info->active) {
        if (strcmp(info->shm_ptr, "END") == 0) {
            strcpy(info->shm_ptr, ""); // Clear command
            pthread_mutex_lock(&lock);
            if (--info->connections_handled <= 0 && MAX_CONNECT_PER_THREAD != 0) {
                info->active = 0; // Mark thread as inactive
            }
            pthread_mutex_unlock(&lock);
        }
        usleep(100000); // Prevent busy waiting
    }

    shmdt(info->shm_ptr);
    shmctl(info->shm_id, IPC_RMID, NULL);
    return NULL;
}

void initialize_thread_pool() {
    thread_pool = malloc(MAX_THREAD * sizeof(thread_info_t));
    for (int i = 0; i < MAX_THREAD; i++) {
        thread_pool[i].active = 0;
        thread_pool[i].connections_handled = 0;
        thread_pool[i].shm_id = shmget(IPC_PRIVATE, SHM_SIZE, IPC_CREAT | 0666);
        thread_pool[i].shm_ptr = (char*)shmat(thread_pool[i].shm_id, NULL, 0);
        memset(thread_pool[i].shm_ptr, 0, SHM_SIZE); // Initialize SHM
    }
}

int find_available_thread() {
    for (int i = 0; i < MAX_THREAD; i++) {
        if (!thread_pool[i].active || (thread_pool[i].active && thread_pool[i].connections_handled < MAX_CONNECT_PER_THREAD)) {
            return i;
        }
    }
    return -1;
}

void start_demon() {
    mkfifo(TUBE_NAME, 0666);
    int tube_fd = open(TUBE_NAME, O_RDONLY);
    if (tube_fd < 0) {
        perror("Failed to open tube");
        exit(EXIT_FAILURE);
    }

    printf("Server starting...\n");
    while (1) {
        char buffer[10];
        if (read(tube_fd, buffer, sizeof(buffer)) > 0 && strcmp(buffer, "SYNC") == 0) {
            pthread_mutex_lock(&lock);
            int index = find_available_thread();
            if (index != -1 && !thread_pool[index].active) {
                thread_pool[index].active = 1;
                thread_pool[index].connections_handled = 1; // Initialize to 1 for the new connection
                pthread_create(&thread_pool[index].thread_id, NULL, handle_client, &thread_pool[index]);
                printf("Thread %d started with SHM ID %d\n", index, thread_pool[index].shm_id);
                total_threads_initialized++;
            } else if (index != -1) {
                thread_pool[index].connections_handled++;
                printf("Allocating additional connection to thread %d with SHM ID %d\n", index, thread_pool[index].shm_id);
            } else {
                printf("No available thread. MAX_THREAD limit reached.\n");
            }
            pthread_mutex_unlock(&lock);
        }
    }

    // Clean-up
    close(tube_fd);
    unlink(TUBE_NAME);
    for (int i = 0; i < MAX_THREAD; i++) {
        if (thread_pool[i].active) {
            pthread_join(thread_pool[i].thread_id, NULL);
            // Detach and remove the shared memory segment
            shmdt(thread_pool[i].shm_ptr);
            shmctl(thread_pool[i].shm_id, IPC_RMID, NULL);
        }
    }
    free(thread_pool);
    printf("Server shutting down...\n");
}

int main() {
    pthread_mutex_init(&lock, NULL);
    load_config(); // Load configuration settings
    initialize_thread_pool(); // Initialize the thread pool based on the loaded configuration
    start_demon(); // Start the server daemon to listen for connections and handle client requests

    // Cleanup before exiting
    pthread_mutex_destroy(&lock);

    return 0;
}

