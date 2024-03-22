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

struct DaemonConfig read_config() {
    struct DaemonConfig config;
    FILE *fp;
    char param[30];
    int value;

    fp = fopen("demon.conf", "r");
    if (fp == NULL) {
        perror("Error opening config file");
        exit(1);
    }

    while (fscanf(fp, "%s = %d\n", param, &value) != EOF) {
        if (strcmp(param, "MIN_THREAD") == 0)
            config.MIN_THREAD = value;
        else if (strcmp(param, "MAX_THREAD") == 0)
            config.MAX_THREAD = value;
        else if (strcmp(param, "MAX_CONNECT_PER_THREAD") == 0)
            config.MAX_CONNECT_PER_THREAD = value;
        else if (strcmp(param, "SHM_SIZE") == 0)
            config.SHM_SIZE = value;
    }

    fclose(fp);
    return config;
}

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

void *handle_client(void *arg) {
    struct ConnectionInfo *conn_info = (struct ConnectionInfo *)arg;
    int shmid = conn_info->shmid;
    char *shm_name = conn_info->shm_name;
    int connections_left = conn_info->connections_left;

    char *shm, *s;
    int n;

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

        // Exécuter la commande demandée par le client
        char command_result[SHMSZ];
        execute_command(client_request, command_result);

        // Déposer les résultats dans la SHM
        write_results_to_shm(shm, command_result);

        // Décrémenter le nombre de connexions restantes
        connections_left--;

        if (connections_left == 0 && config.MAX_CONNECT_PER_THREAD != 0) {
            // Terminer proprement le thread
            pthread_exit(NULL);
        }
    }
}

int main() {
    struct DaemonConfig config = read_config();
    key_t key;
    int shmid;
    char *shm, *s;

    pthread_t tid;

    // Vérification des paramètres lus depuis demon.conf
    if (config.MIN_THREAD > config.MAX_THREAD) {
        printf("Error: MIN_THREAD cannot be greater than MAX_THREAD\n");
        exit(1);
    }

    if (config.MAX_CONNECT_PER_THREAD == 0) {
        printf("Warning: Threads will not terminate as MAX_CONNECT_PER_THREAD is set to 0\n");
    }

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

    // Initialisation des threads
    for (int i = 0; i < config.MIN_THREAD; i++) {
        struct ConnectionInfo *conn_info = malloc(sizeof(struct ConnectionInfo));
        conn_info->shmid = shmid;
        strcpy(conn_info->shm_name, "shared_memory"); // Nom de la SHM
        conn_info->connections_left = config.MAX_CONNECT_PER_THREAD;
        pthread_create(&tid, NULL, handle_client, (void *)conn_info);
    }

    // Attente de requêtes des clients
    while (1) {
        // Accepter les demandes des clients
        // Créer une structure ConnectionInfo pour chaque client
        // Démarrer un thread pour traiter chaque demande
        struct ConnectionInfo *conn_info = malloc(sizeof(struct ConnectionInfo));
        conn_info->shmid = shmid;
        strcpy(conn_info->shm_name, "shared_memory"); // Nom de la SHM
        conn_info->connections_left = config.MAX_CONNECT_PER_THREAD;
        pthread_create(&tid, NULL, handle_client, (void *)conn_info);
    }

    exit(0);
}