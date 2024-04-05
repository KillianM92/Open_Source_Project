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

char* getCurrentWorkingDirectory() {
    char *cwd;
    char buffer[1024];

    cwd = getcwd(buffer, sizeof(buffer));
    if (cwd == NULL) {
        perror("getcwd() error");
        return NULL; // Retourne NULL en cas d'erreur
    }

    // Allouer de la mémoire pour le chemin du répertoire à renvoyer
    char* cwdCopy = malloc(strlen(cwd) + 1); // +1 pour le caractère nul de fin
    if (cwdCopy == NULL) {
        perror("malloc() error");
        return NULL; // Retourne NULL si l'allocation échoue
    }

    strcpy(cwdCopy, cwd); // Copier le chemin dans le nouvel espace mémoire
    return cwdCopy; // Retourner le pointeur vers le chemin alloué dynamiquement
}

sem_t* test_sem = NULL;

void* handle_client(void* arg) {
    struct demon_config config = read_config();
    
    int client_pid = *(int*)arg;
    free(arg); // Libérer la mémoire allouée pour le PID du client

    // Ouvrir le sémaphore pour la synchronisation
    test_sem = sem_open("/sem_demon", 0); // Assurez-vous que le nom correspond à celui utilisé par le client
    if (test_sem == SEM_FAILED) {
        perror("Server: sem_open failed");
        pthread_exit(NULL);
    }

    char shm_name[256], response_pipe_name[256];
    snprintf(shm_name, sizeof(shm_name), "/demon_shm_%d", client_pid);
    snprintf(response_pipe_name, sizeof(response_pipe_name), "/tmp/response_pipe_%d", client_pid);

    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open_test_handle_client");
        pthread_exit(NULL);
    }
    // Supposons que la configuration de la taille de la SHM dans le fichier de conf est la taille désirée pour la SHM
    if (ftruncate(shm_fd, config.shm_size) == -1) {
        perror("ftruncate");
        close(shm_fd);
        pthread_exit(NULL);
    }

    void* shm_ptr = mmap(NULL, config.shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        pthread_exit(NULL);
    }

    int response_pipe_fd = open(response_pipe_name, O_WRONLY);
    if (response_pipe_fd == -1) {
        perror("open response pipe");
        munmap(shm_ptr, config.shm_size);
        close(shm_fd);
        pthread_exit(NULL);
    }

    if (write(response_pipe_fd, shm_name, strlen(shm_name) + 1) == -1) {
        perror("write to response pipe");
    }

    close(response_pipe_fd);

    // Attendre (wait) sur le sémaphore avant de lire le choix depuis la SHM
    sem_wait(test_sem);

    // Mappage de la SHM pour lire le choix de l'utilisateur
    int* choice_ptr = (int*)mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (choice_ptr == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
    }

    int choice = *choice_ptr;

    char message[config.shm_size];
    switch(choice) {
        case 1:
            snprintf(message, sizeof(message), "Date : 02/04/2024"); // Exécutez et stockez le résultat de 'date'
            break;
        case 2:
            snprintf(message, sizeof(message), "ID : 1000"); // Similaire pour l'heure
            break;
        case 3:
            snprintf(message, sizeof(message), "Current working directory: %s\n", getCurrentWorkingDirectory());
            break;
        case 4:
            snprintf(message, sizeof(message), "client1  client1.c  demon.conf  makefile  serveur  serveur.c");
            break;
        default:
            snprintf(message, sizeof(message), "Choix non reconnu");
    }

    memcpy(shm_ptr, message, strlen(message) + 1);

    sleep(10);

    munmap(shm_ptr, config.shm_size);
    munmap(choice_ptr, sizeof(int));
    close(shm_fd);
    // Fermer le sémaphore après utilisation
    sem_close(test_sem);

    printf("Traitement du client (PID: %d) terminé.\n", client_pid);
    pthread_exit(NULL);
    //shm_unlink(shm_name);
}

void startdemon() {
    struct demon_config config = read_config();

    char buffer[config.shm_size];

    printf("Serveur en attente de demandes de connexion...\n");

    if (mkfifo(PIPE_NAME, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo");
        exit(EXIT_FAILURE);
    }

    int pipe_fd = open(PIPE_NAME, O_RDONLY | O_NONBLOCK);
    if (pipe_fd == -1) {
        perror("Opening demon pipe failed");
        exit(EXIT_FAILURE);
    }

    while (1) {
        ssize_t read_bytes = read(pipe_fd, buffer, config.shm_size - 1);
        if (read_bytes > 0) {
            buffer[read_bytes] = '\0'; // Assurer la terminaison de la chaîne

            // Extraire le PID et vérification de la présence de "SYNC"
            int* client_pid = malloc(sizeof(int));
            if (sscanf(buffer, "SYNC:%d", client_pid) == 1) {
                printf("SYNC request received from PID : %d\n", *client_pid);
            }; // Lecture du PID client depuis le buffer

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
            
            pthread_t thread_id;
            if (pthread_create(&thread_id, NULL, handle_client, (void*)client_pid) != 0) {
                perror("pthread_create");
                exit(EXIT_FAILURE);
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

int main() {
    struct demon_config config = read_config();
    
    printf("Serveur prêt. Configuration : MIN_THREAD=%d, MAX_THREAD=%d, MAX_CONNECT_PER_THREAD=%d, SHM_SIZE=%d\n",
           config.min_thread, config.max_thread, config.max_connect_per_thread, config.shm_size);

    startdemon(); // Commencer à écouter les demandes de connexion
    return 0;
}