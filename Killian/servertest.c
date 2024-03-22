#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <pthread.h>
#include <fcntl.h>
#include <string.h>

#define SHMSZ 1024

// Structure pour stocker les informations de connexion
struct ConnectionInfo {
    int shmid;
    char shm_name[20];
    int connections_left;
};

// Structure pour stocker les paramètres lus depuis demon.conf
struct DaemonConfig {
    int MIN_THREAD;
    int MAX_THREAD;
    int MAX_CONNECT_PER_THREAD;
    int SHM_SIZE;
};

struct DaemonConfig config; // Variable globale pour stocker la configuration

// Fonction pour lire la requête du client depuis la SHM
void read_client_request(char *shm, char *request) {
    strcpy(request, shm);
}

// Fonction pour exécuter la commande demandée par le client
void execute_command(char *command, char *result) {
    FILE *fp;
    char buffer[256];
    result[0] = '\0'; // Initialiser la chaîne de résultats

    fp = popen(command, "r");
    if (fp == NULL) {
        perror("Error executing command");
        exit(1);
    }

    // Lire le résultat de la commande
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        strcat(result, buffer);
    }

    pclose(fp);
}

// Fonction pour déposer les résultats dans la SHM
void write_results_to_shm(char *shm, char *results) {
    strcpy(shm, results);
}

// Fonction pour traiter les demandes des clients
void *handle_client(void *arg) {
    struct ConnectionInfo *conn_info = (struct ConnectionInfo *)arg;
    int shmid = conn_info->shmid;
    char *shm_name = conn_info->shm_name;
    int connections_left = conn_info->connections_left;

    char *shm;

    // Attacher la SHM
    if ((shm = shmat(shmid, NULL, 0)) == (char *) -1) {
        perror("shmat");
        exit(1);
    }

    // Attendre la requête du client
    while (1) {
        // Lire la requête du client depuis la SHM
        char client_request[SHMSZ];
        read_client_request(shm, client_request);

        // Si la requête est "END", terminer la connexion
        if (strcmp(client_request, "END") == 0) {
            break;
        }

        // Exécuter la commande demandée par le client
        char command_result[SHMSZ];
        execute_command(client_request, command_result);

        // Déposer les résultats dans la SHM
        write_results_to_shm(shm, command_result);

        // Décrémenter le nombre de connexions restantes
        connections_left--;

        // Si le nombre de connexions restantes atteint 0 et MAX_CONNECT_PER_THREAD n'est pas 0, terminer le thread
        if (connections_left == 0 && config.MAX_CONNECT_PER_THREAD != 0) {
            break;
        }
    }

    // Terminer le thread
    pthread_exit(NULL);
}

// Fonction pour initialiser les threads
void initialize_threads(pthread_t *tid, struct ConnectionInfo *conn_info) {
    for (int i = 0; i < config.MIN_THREAD; i++) {
        pthread_create(&tid[i], NULL, handle_client, (void *)conn_info);
    }
}

// Fonction pour gérer le pool de threads
void *thread_pool_manager(void *arg) {
    struct ConnectionInfo *conn_info = (struct ConnectionInfo *)arg;
    pthread_t tid[config.MAX_THREAD]; // Tableau pour stocker les IDs des threads
    int num_threads = config.MIN_THREAD;

    while (1) {
        // Si le nombre de threads descend en dessous de MIN_THREAD, en créer autant que nécessaire
        if (num_threads < config.MIN_THREAD) {
            for (int i = num_threads; i < config.MIN_THREAD; i++) {
                pthread_create(&tid[i], NULL, handle_client, (void *)conn_info);
                num_threads++;
            }
        }

        // Accepter les demandes des clients et démarrer un thread pour chaque demande
        // (Non implémenté ici pour des raisons de clarté, à implémenter en fonction de l'environnement)
    }

    pthread_exit(NULL);
}

// Fonction principale
int main() {
    key_t key;
    int shmid;
    char *shm;

    // Lecture de la configuration depuis demon.conf
    FILE *fp = fopen("demon.conf", "r");
    if (fp == NULL) {
        perror("Error opening config file");
        exit(1);
    }
    fscanf(fp, "MIN_THREAD = %d\n", &config.MIN_THREAD);
    fscanf(fp, "MAX_THREAD = %d\n", &config.MAX_THREAD);
    fscanf(fp, "MAX_CONNECT_PER_THREAD = %d\n", &config.MAX_CONNECT_PER_THREAD);
    fscanf(fp, "SHM_SIZE = %d\n", &config.SHM_SIZE);
    fclose(fp);

    // Création de la zone de mémoire partagée
    if ((key = ftok(".", 'R')) == -1) {
        perror("ftok");
        exit(1);
    }

    if ((shmid = shmget(key, SHMSZ, IPC_CREAT | 0666)) < 0) {
        perror("shmget");
        exit(1);
    }

    if ((shm = shmat(shmid, NULL, 0)) == (char *) -1) {
        perror("shmat");
        exit(1);
    }

    // Création de la structure ConnectionInfo
    struct ConnectionInfo *conn_info = malloc(sizeof(struct ConnectionInfo));
    conn_info->shmid = shmid;
    strcpy(conn_info->shm_name, "shared_memory");
    conn_info->connections_left = config.MAX_CONNECT_PER_THREAD;

    // Initialisation des threads
    pthread_t pool_manager_tid;
    initialize_threads(&pool_manager_tid, conn_info);

    // Lancement du gestionnaire de pool de threads
    pthread_create
