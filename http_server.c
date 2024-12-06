#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>

#define BUFFER_SIZE 4096
#define DEFAULT_PORT 80

// Global statistics
pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;
int total_requests = 0;
int total_bytes_received = 0;
int total_bytes_sent = 0;

// Struct for client request information
typedef struct {
    int client_socket;
    struct sockaddr_in client_addr;
} client_info_t;

// Utility function: send HTTP response
void send_response(int client_socket, const char *status, const char *content_type, const char *body, size_t body_length) {
    char header[BUFFER_SIZE];
    snprintf(header, BUFFER_SIZE, "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n\r\n", status, content_type, body_length);

    send(client_socket, header, strlen(header), 0);
    send(client_socket, body, body_length, 0);
}

// Handle "/static"
void handle_static(int client_socket, const char *path) {
    char full_path[BUFFER_SIZE] = "./static";
    strcat(full_path, path);

    int file = open(full_path, O_RDONLY);
    if (file < 0) {
        send_response(client_socket, "404 Not Found", "text/plain", "File not found", strlen("File not found"));
        return;
    }

    struct stat file_stat;
    fstat(file, &file_stat);
    size_t file_size = file_stat.st_size;

    char *file_data = malloc(file_size);
    read(file, file_data, file_size);
    close(file);

    send_response(client_socket, "200 OK", "application/octet-stream", file_data, file_size);
    free(file_data);
}

// Handle "/stats"
void handle_stats(int client_socket) {
    pthread_mutex_lock(&stats_lock);
    char body[BUFFER_SIZE];
    snprintf(body, BUFFER_SIZE,
             "<html><body>"
             "<h1>Server Statistics</h1>"
             "<p>Total requests: %d</p>"
             "<p>Total bytes received: %d</p>"
             "<p>Total bytes sent: %d</p>"
             "</body></html>",
             total_requests, total_bytes_received, total_bytes_sent);
    pthread_mutex_unlock(&stats_lock);

    send_response(client_socket, "200 OK", "text/html", body, strlen(body));
}

// Handle "/calc"
void handle_calc(int client_socket, const char *query) {
    int a = 0, b = 0;
    sscanf(query, "a=%d&b=%d", &a, &b);
    int sum = a + b;

    char body[BUFFER_SIZE];
    snprintf(body, BUFFER_SIZE, "<html><body><h1>Calculation Result</h1><p>%d + %d = %d</p></body></html>", a, b, sum);

    send_response(client_socket, "200 OK", "text/html", body, strlen(body));
}

// Process HTTP requests
void *handle_client(void *arg) {
    client_info_t *client_info = (client_info_t *)arg;
    int client_socket = client_info->client_socket;

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);

    if (bytes_read <= 0) {
        close(client_socket);
        free(client_info);
        pthread_exit(NULL);
    }

    buffer[bytes_read] = '\0';

    pthread_mutex_lock(&stats_lock);
    total_requests++;
    total_bytes_received += bytes_read;
    pthread_mutex_unlock(&stats_lock);

    char method[16], path[256], protocol[16];
    sscanf(buffer, "%s %s %s", method, path, protocol);

    if (strcmp(method, "GET") != 0) {
        send_response(client_socket, "405 Method Not Allowed", "text/plain", "Only GET method is supported", strlen("Only GET method is supported"));
    } else if (strncmp(path, "/static", 7) == 0) {
        handle_static(client_socket, path + 7);
    } else if (strncmp(path, "/stats", 6) == 0) {
        handle_stats(client_socket);
    } else if (strncmp(path, "/calc?", 6) == 0) {
        handle_calc(client_socket, path + 6);
    } else {
        send_response(client_socket, "404 Not Found", "text/plain", "Endpoint not found", strlen("Endpoint not found"));
    }

    pthread_mutex_lock(&stats_lock);
    total_bytes_sent += bytes_read;
    pthread_mutex_unlock(&stats_lock);

    close(client_socket);
    free(client_info);
    pthread_exit(NULL);
}

// Main server function
int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;

    if (argc == 3 && strcmp(argv[1], "-p") == 0) {
        port = atoi(argv[2]);
        if (port <= 0) {
            fprintf(stderr, "Invalid port number.\n");
            return 1;
        }
    }

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Failed to create socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        return 1;
    }

    if (listen(server_socket, 10) < 0) {
        perror("Listen failed");
        close(server_socket);
        return 1;
    }

    printf("Server listening on port %d...\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        client_info_t *client_info = malloc(sizeof(client_info_t));
        client_info->client_socket = client_socket;
        client_info->client_addr = client_addr;

        pthread_t client_thread;
        pthread_create(&client_thread, NULL, handle_client, client_info);
        pthread_detach(client_thread);
    }

    close(server_socket);
    return 0;
}
