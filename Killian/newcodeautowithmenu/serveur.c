#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>

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

typedef struct {
    int client_pid;
    int choice;
} thread_data_t;

void* handle_client(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    int client_pid = data->client_pid;
    int choice = data->choice;

    char shm_name[256], response_pipe_name[256];
    snprintf(shm_name, sizeof(shm_name), "/demon_shm_%d", client_pid);
    snprintf(response_pipe_name, sizeof(response_pipe_name), "/tmp/response_pipe_%d", client_pid);

    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open_test_handle_client");
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

    char message[BUFFER_SIZE];
    switch(choice) {
        case 1:
            snprintf(message, sizeof(message), "Date : 02/04/2024"); // Exécutez et stockez le résultat de 'date'
            break;
        case 2:
            snprintf(message, sizeof(message), "Heure : 01:00AM"); // Similaire pour l'heure
            break;
        case 3:
            snprintf(message, sizeof(message), "TEST3");
            break;
        case 4:
            snprintf(message, sizeof(message), "TEST4");
            break;
        default:
            snprintf(message, sizeof(message), "Choix non reconnu");
    }

    memcpy(shm_ptr, message, strlen(message) + 1);

    sleep(10);

    munmap(shm_ptr, BUFFER_SIZE);
    close(shm_fd);

    printf("Traitement du client (PID: %d) terminé.\n", client_pid);
    free(arg);
    pthread_exit(NULL);
    //shm_unlink(shm_name);
}

void startdemon() {
    struct demon_config config = read_config();

    printf("Serveur en attente de demandes de connexion...\n");

     if (mkfifo(PIPE_NAME, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo");
        exit(EXIT_FAILURE);
    }

    int pipe_fd = open(PIPE_NAME, O_RDONLY | O_NONBLOCK);
    if (pipe_fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    while (1) {
        char buffer[BUFFER_SIZE];
        ssize_t read_bytes = read(pipe_fd, buffer, BUFFER_SIZE - 1);
        if (read_bytes > 0) {
            buffer[read_bytes] = '\0';
            int client_pid;
            sscanf(buffer, "%d", &client_pid); // Lecture du PID client depuis le buffer

            // Préparez le nom de la SHM basé sur le PID reçu
            char shm_name[256];
            snprintf(shm_name, sizeof(shm_name), "/demon_shm_%d", client_pid);

            // Création de la SHM pour ce client spécifique
            int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
            if (shm_fd == -1) {
                perror("shm_open_test_start_demon");
                continue;
            }
            if (ftruncate(shm_fd, sizeof(int)) == -1) {
                perror("ftruncate");
                close(shm_fd);
                continue;
            }

            // Mappage de la SHM pour lire le choix de l'utilisateur
            int* choice_ptr = (int*)mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
            if (choice_ptr == MAP_FAILED) {
                perror("mmap");
                close(shm_fd);
                continue;
            }

            // Assurez-vous que le client a le temps d'écrire son choix
            sleep(1); // Peut-être ajuster ce délai selon le comportement observé

            int choice = *choice_ptr;
            munmap(choice_ptr, sizeof(int));

            // Préparation et passage des données au thread
            thread_data_t* data = malloc(sizeof(thread_data_t));
            if (!data) {
                perror("malloc");
                continue;
            }
            data->client_pid = client_pid;
            data->choice = choice;

            pthread_t thread_id;
            if (pthread_create(&thread_id, NULL, handle_client, (void*)data) != 0) {
                perror("pthread_create");
                free(data);
                close(shm_fd);
                continue;
            }
            pthread_detach(thread_id);

            close(shm_fd); // Fermez la SHM ici si elle n'est plus nécessaire
        } else if (read_bytes == 0) {
            sleep(1);
        } else if (errno != EAGAIN) {
            perror("read");
            exit(EXIT_FAILURE);
        }
    }

    close(pipe_fd);
}

char* getUsername(uid_t uid) {
    struct passwd *pw = getpwuid(uid);
    if (pw != NULL) {
        return pw->pw_name;
    }
    return "Unknown User";
}

char* getGroupName(gid_t gid) {
    struct group *grp = getgrgid(gid);
    if (grp != NULL) {
        return grp->gr_name;
    }
    return "Unknown Group";
}

int main() {
    struct demon_config config = read_config();
    
    printf("Serveur prêt. Configuration : MIN_THREAD=%d, MAX_THREAD=%d, MAX_CONNECT_PER_THREAD=%d, SHM_SIZE=%d\n",
           config.min_thread, config.max_thread, config.max_connect_per_thread, config.shm_size);

    startdemon(); // Commencer à écouter les demandes de connexion
    return 0;
}