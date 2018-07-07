/*
 * c-web.c
 *
 * Created on: July 7, 2018
 *     Author: nick-hiebl
 *
 * A C webserver using standard libraries.
 *
 *
 * Made using heavy reference to:
 * https://gist.github.com/Xeoncross/4113893
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>

#define BUFFER_SIZE 1024

typedef struct {
    char *ext;
    char *mediatype;
} extn;

extn extensions[] = {
    {"gif", "image/gif"},
    {"txt", "text/plain"},
    {"htm", "text/html"},
    {"html", "text/html"},
    {"js", "text/javascript"},
    {"css", "text/css"},
    {0, 0}
};

void showError(char *msg) {
    perror(msg);
    exit(1);
}

// Use stat to access file size
int get_file_size(int fd) {
    struct stat info;
    if (fstat(fd, &info) == -1) {
        return 1;
    } else {
        return (int)info.st_size;
    }
}

// Send a message through the socket
void send_msg(int fd, char *msg) {
    int len = strlen(msg);
    if (send(fd, msg, len, 0) == -1) {
        fprintf(stderr, "Error in send\n");
    }
}

// Returns number of bytes read into buffer
int receive_new(int fd, char *buffer) {
    int i = 0;
    while (recv(fd, &(buffer[i]), 1, 0) != 0) {
        // If this is the end of EOL
        if (i > 0 && buffer[i] == '\n') {
            // If previous byte is the first part of EOL
            if (buffer[i-1] == '\r') {
                // Null terminate and return length
                buffer[i-1] = '\0';
                return i-2;
            }
        }
        i++;
    }
    // Line was never properly terminated return 0
    return 0;
}

char *webroot() {
    // Read a line
    FILE *fin = fopen("conf", "rt");
    char buffer[BUFFER_SIZE];
    fgets(buffer, BUFFER_SIZE, fin);
    fclose(fin);
    // Gets last newline character
    char *nl_ptr = strrchr(buffer, '\n');
    if (nl_ptr != NULL) {
        *nl_ptr = '\0';
    }
    return strdup(buffer);
}

// Deal with file not found issues
void handle404(int fd) {
    printf("404 File not found error\n");
    send_msg(fd, "HTTP/1.1 404 Not Found\r\n");
    send_msg(fd, "Server : c-web\r\n\r\n");
    send_msg(fd, "<html><head><title>404 Error</title></head>");
    send_msg(fd, "<body><p>404 Not found: The requested resource could not be located!</p></body></html>");
}

void handle415(int fd) {
    fprintf(stderr, "415 Unsupported Media Type\n");
    send_msg(fd, "HTTP/1.1 415 Unsupported media\r\n");
    send_msg(fd, "Server : c-web\r\n\r\n");
    send_msg(fd, "<html><head><title>415 Unsupported media</title></head>");
    send_msg(fd, "<body><p>415 Unsupported media type</p></body></html>");
}

int connection(int fd) {
    char request[BUFFER_SIZE], resource[BUFFER_SIZE], *ptr;
    int fd1, length;
    if (receive_new(fd, request) == 0) {
        fprintf(stderr, "Receive failed\n");
    }
    printf("%s\n", request);
    ptr = strstr(request, " HTTP/");
    if (ptr == NULL) {
        printf("Not an HTTP request\n");
    } else {
        // Split string on the space
        *ptr = 0;
        ptr = NULL;

        // If this is a GET request
        if (strncmp(request, "GET ", 4) == 0) {
            printf("GET request\n");
            ptr = request + 4;
        }
        if (ptr == NULL) {
            fprintf(stderr, "Unknown request type\n");
        } else {
            printf("Here %s\n", ptr);
            // If this is an implied URL, append index.html
            if (ptr[strlen(ptr) - 1] == '/') {
                strcat(ptr, "index.html");
            }
            strcpy(resource, "./");
            // strcpy(resource, webroot());
            strcat(resource, ptr);
            printf("Resource: %s\n", resource);
            char *s = strchr(ptr, '.');
            int i;
            for (i = 0; extensions[i].ext != NULL; i++) {
                if (strcmp(s + 1, extensions[i].ext) == 0) {
                    fd1 = open(resource, O_RDONLY, 0);
                    printf("Opening \"%s\"\n", resource);
                    if (fd1 == -1) {
                        handle404(fd);
                    } else {
                        printf("200 OK, Content-Type: %s\n\n",
                            extensions[i].mediatype);
                        send_msg(fd, "HTTP/1.1 200 OK\r\n");
                        send_msg(fd, "Server: c-web\r\n\r\n");

                        // GET request
                        if (ptr == request + 4) {
                            printf("Handling GET request\n");
                            if ((length = get_file_size(fd1)) == -1) {
                                fprintf(stderr, "Error in getting size\n");
                            }
                            size_t total_bytes_sent = 0;
                            ssize_t bytes_sent;
                            while (total_bytes_sent < length) {
                                if ((bytes_sent = sendfile(fd, fd1, 0, length - total_bytes_sent)) <= 0) {
                                    printf("sending\n");
                                    if (errno == EINTR || errno == EAGAIN) {
                                        continue;
                                    }
                                    perror("sendfile");
                                    return -1;
                                }
                                total_bytes_sent += bytes_sent;
                            }
                        }
                    }
                    break;
                }
                int size = sizeof(extensions) / sizeof(extensions[0]);
                if (i == size - 2) {
                    handle415(fd);
                }
            }
            close(fd);
        }
    }
    shutdown(fd, SHUT_RDWR);
}

int main(int argc, char *argv[]) {
    socklen_t clilen;
    struct sockaddr_in serv_addr;

    if (argc < 2) {
        fprintf(stderr, "No port provided\n");
        printf("Correct usage should be: %s <port>\n", argv[0]);
        exit(1);
    }
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        showError("Error opening socket");
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    int portno = atoi(argv[1]);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        showError("Error on binding");
    }
    listen(sockfd, 5);
    clilen = sizeof(serv_addr);

    while (1) {
        struct sockaddr_in cli_addr;
        int newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) {
            showError("Error on accept");
        }
        int pid = fork();
        if (pid < 0) {
            showError("Error on fork");
        }
        if (pid == 0) {
            close(sockfd);
            connection(newsockfd);
            exit(0);
        } else {
            close(newsockfd);
        }
    }
    close(sockfd);
    return 0;
}
