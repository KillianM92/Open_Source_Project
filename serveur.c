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
#include <time.h>
#include <signal.h>

#define CONFIG_FILE "demon.conf"
#define PIPE_NAME "/tmp/demon_pipe"

volatile sig_atomic_t keep_running = 1;

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

void sigint_handler(int sig) {
    keep_running = 0;
}

char* getCurrentDate() {
    time_t now;
    struct tm *tm_info;

    // 15 caractères pour la date et l'heure "%d-%m-%Y"
    // +1 pour le caractère de fin de chaîne '\0'.
    char buffer_date[16];

    // Obtenir le temps courant
    time(&now);
    
    // Convertir time_t en structure tm pour la date et l'heure locales
    tm_info = localtime(&now);

    // Formater la date et l'heure dans la chaîne buffer
    strftime(buffer_date, sizeof(buffer_date), "%d-%m-%Y", tm_info);

    // Allouer de la mémoire pour la chaîne de date et d'heure à renvoyer
    char* dateTimeCopy = malloc(strlen(buffer_date) + 1);
    if (dateTimeCopy == NULL) {
        perror("malloc() error for dateTimeCopy");
        return NULL;
    }

    // Copier le contenu du buffer temporaire vers la mémoire allouée dynamiquement
    strcpy(dateTimeCopy, buffer_date);
    return dateTimeCopy; // Retourner le pointeur vers la chaîne allouée dynamiquement
}

char* getCurrentTime() {
    time_t now;
    struct tm *tm_info;

    // 10 caractères pour la date et l'heure "%H:%M:%S"
    // +1 pour le caractère de fin de chaîne '\0'.
    char buffer_time[11];

    // Obtenir le temps courant
    time(&now);
    
    // Convertir time_t en structure tm pour la date et l'heure locales
    tm_info = localtime(&now);

    // Formater la date et l'heure dans la chaîne buffer
    strftime(buffer_time, sizeof(buffer_time), "%H:%M:%S", tm_info);

    // Allouer de la mémoire pour la chaîne de date et d'heure à renvoyer
    char* dateTimeCopy = malloc(strlen(buffer_time) + 1);
    if (dateTimeCopy == NULL) {
        perror("malloc() error for dateTimeCopy");
        return NULL;
    }

    // Copier le contenu du buffer temporaire vers la mémoire allouée dynamiquement
    strcpy(dateTimeCopy, buffer_time);
    return dateTimeCopy; // Retourner le pointeur vers la chaîne allouée dynamiquement
}

char* printUserIdInfo() {
    uid_t uid = getuid(); // Obtenir l'UID de l'utilisateur actuel
    gid_t gid = getgid(); // Obtenir le GID de l'utilisateur actuel
    struct passwd *pw = getpwuid(uid);
    struct group *gr = getgrgid(gid);

    // D'abord, obtenir le nombre de groupes
    int ngroups = getgroups(0, NULL);
    if (ngroups == -1) {
        perror("getgroups");
        exit(EXIT_FAILURE);
    }

    // Allouer de la mémoire pour les groupes
    gid_t *groups = malloc(ngroups * sizeof(gid_t));
    if (groups == NULL) {
        perror("malloc for groups");
        exit(EXIT_FAILURE);
    }

    // Maintenant, obtenir les GID des groupes
    if (getgroups(ngroups, groups) == -1) {
        perror("getgroups");
        free(groups);
        exit(EXIT_FAILURE);
    }

    size_t infoSize = 1024;
    for (int i = 0; i < ngroups; ++i) {
        gr = getgrgid(groups[i]);
        if (gr) {
            infoSize += strlen(gr->gr_name) + 10; // Espace supplémentaire pour les numéros et la ponctuation
        }
    }

    char *info = malloc(infoSize);
    if (info == NULL) {
        perror("malloc for info");
        free(groups);
        return NULL;
    }

    // Construction de la chaîne d'informations
    int offset = snprintf(info, infoSize, "uid=%d(%s) gid=%d(%s) groups=", uid, (pw != NULL) ? pw->pw_name : "Unknown", gid, (gr != NULL) ? gr->gr_name : "Unknown");
    for (int i = 0; i < ngroups; ++i) {
        gr = getgrgid(groups[i]);
        if (gr) {
            offset += snprintf(info + offset, infoSize - offset, "%d(%s)%s", groups[i], gr->gr_name, (i < ngroups - 1) ? "," : "");
        }
    }

    free(groups);  // Libérer la mémoire allouée pour les groupes
    return info;
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

sem_t* choice_sem = NULL;
sem_t* reponse_sem = NULL;

void* handle_client(void* arg) {
    struct demon_config config = read_config();
    
    int client_pid = *(int*)arg;
    free(arg); // Libérer immédiatement la mémoire allouée pour le PID du client

    // Ouvrir les sémaphores pour la synchronisation des choix et des réponses
    choice_sem = sem_open("/choice_sem_demon", 0);
    if (choice_sem == SEM_FAILED) {
        perror("Server: sem_open (choice_sem) failed");
        pthread_exit(NULL);
    }

    reponse_sem = sem_open("/reponse_sem_demon", 0);
    if (reponse_sem == SEM_FAILED) {
        perror("Server: sem_open (response_sem) failed");
        pthread_exit(NULL);
    }

    // Construction des noms pour la SHM et le tube de réponse basés sur le PID du client
    char shm_name[256], response_pipe_name[256];
    snprintf(shm_name, sizeof(shm_name), "/demon_shm_%d", client_pid);
    snprintf(response_pipe_name, sizeof(response_pipe_name), "/tmp/response_pipe_%d", client_pid);

    // Création ou ouverture de la SHM
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        // La SHM n'existe pas, tentons de la créer
        shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
        if (shm_fd == -1) {
        perror("Echec lors de la création/ouverture de la SHM");
        pthread_exit(NULL);
        }
    }
    // Supposons que la configuration de la taille de la SHM dans le fichier de conf est la taille désirée pour la SHM
    if (ftruncate(shm_fd, config.shm_size) == -1) {
        perror("ftruncate");
        close(shm_fd);
        pthread_exit(NULL);
    }

    // Mapping de la SHM dans l'espace d'adressage du processus
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

    int choice;
    do {
    // Attendre le choix du client
    sem_wait(choice_sem);

    memcpy(&choice, shm_ptr, sizeof(int));

    char message[config.shm_size];
    switch(choice) {
        case 1:
            snprintf(message, sizeof(message), "Date : %s", getCurrentDate());
            break;
        case 2:
            snprintf(message, sizeof(message), "Heure : %s", getCurrentTime());
            break;
        case 3:
            snprintf(message, sizeof(message), "ID : %s", printUserIdInfo());
            break;
        case 4:
            snprintf(message, sizeof(message), "Current working directory: %s", getCurrentWorkingDirectory());
            break;
        case 5: // Signal de fin de session
            strcpy(message, "Session terminée.");
            break;
        default:
            snprintf(message, sizeof(message), "Choix non reconnu");
    }

    // Écrire la réponse dans la SHM
    memcpy(shm_ptr, message, strlen(message) + 1);
    // Signaler au client que la réponse est prête
    sem_post(reponse_sem);

    } while(choice != 5); // Continuer jusqu'à ce que le choix soit 5 pour quitter

    // Nettoyage des ressources à la fin de la session
    munmap(shm_ptr, config.shm_size); // Nettoyer la mémoire mappée
    close(shm_fd); // Fermer le descripteur de fichier de la SHM
    sem_close(choice_sem); // Fermer les sémaphores
    sem_close(reponse_sem);
    shm_unlink(shm_name); // Supprimer la SHM

    printf("Traitement du client (PID: %d) terminé.\n", client_pid);
    pthread_exit(NULL);
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

    signal(SIGINT, sigint_handler);

    while (keep_running) {
        ssize_t read_bytes = read(pipe_fd, buffer, config.shm_size - 1);
        if (read_bytes > 0) {
            buffer[read_bytes] = '\0'; // Assurer la terminaison de la chaîne

            // Extraire le PID et vérification de la présence de "SYNC"
            int* client_pid = malloc(sizeof(int));
            if (sscanf(buffer, "SYNC:%d", client_pid) == 1) {
                printf("SYNC request received from PID : %d\n", *client_pid);
            }; // Lecture du PID client depuis le buffer
            
            pthread_t thread_id;
            if (pthread_create(&thread_id, NULL, handle_client, (void*)client_pid) != 0) {
                perror("pthread_create");
                exit(EXIT_FAILURE);
            }
            pthread_detach(thread_id);

        } else if (read_bytes == 0) {
            sleep(1);
        } else if (errno != EAGAIN) {
            perror("read");
            exit(EXIT_FAILURE);
        }
    }

    // Libération du tube du démon
    close(pipe_fd);
    unlink(PIPE_NAME);
}

int main() {
    struct demon_config config = read_config();
    
    printf("Serveur prêt. Configuration : MIN_THREAD=%d, MAX_THREAD=%d, MAX_CONNECT_PER_THREAD=%d, SHM_SIZE=%d\n",
           config.min_thread, config.max_thread, config.max_connect_per_thread, config.shm_size);

    startdemon(); // Commencer à écouter les demandes de connexion

    return 0;
}