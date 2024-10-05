#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h> 
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#define INITIAL_SIZE 1024
int handleLocalFile(char *hostname, char *sub_path) {
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s", sub_path);
   

    // Check if the file exists using stat
    struct stat st;
    if (stat(file_path, &st) == 0) {
        // File exists, proceed to open and read it
        int fd = open(file_path, O_RDONLY);
        if (fd != -1) {
            // Use lseek to determine the file size
            off_t file_size = lseek(fd, 0, SEEK_END);
            lseek(fd, 0, SEEK_SET);

            if (file_size != -1) {
                printf("File is given from local filesystem\n");
                printf("HTTP/1.0 200 OK\n");
                printf("Content-Length: %ld\n", (long)file_size);
                printf("Total response bytes: %ld\n", (long)file_size+strlen("Content-Length: N\r\n\r\n")+strlen("HTTP/1.0 200 OK\r\n"));
            } else {
                perror("Error getting file size");
                close(fd);
                return -1;
            }
            close(fd);
            return 0;
        } else {
            perror("Error opening file");
            return -1;
        }
    } else {
        // File does not exist, proceed with server request
        return -1;
    }
}

int sendHTTPRequest(int sockfd, char *hostname, char *filename) {
    // HTTP request format
    size_t request_size = strlen(hostname) + strlen(filename) + 64;
    char request [request_size];
    printf("filename = %s\n", filename);
    snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", filename, hostname);
    // Send the HTTP request
    ssize_t bytes_sent = send(sockfd, request, strlen(request), 0);
    if (bytes_sent == -1) {
        perror("Error sending HTTP request");
        return -1;
    }
    printf("HTTP request =\n%s\nLEN = %ld\n", request, strlen(request));
    return 0;
}

int isValidHostname(char *hostname) {
    // Check if the hostname ends with a well-known top-level domain
    char *last_dot = strrchr(hostname, '.');
    if (last_dot) {
        char *tld = last_dot + 1;
        if (strcmp(tld, "com") != 0 && strcmp(tld, "net") != 0 && strcmp(tld, "org") != 0 && strcmp(tld, "edu") != 0){
            return 0;
        }
    }
    return 1;
}

void checker(char *hostname, char *sub_path, int *port, char *file_name) {
    struct sockaddr_in sock_info;
    // Use gethostbyname to translate host name to ip address
    struct hostent* server_info = NULL;
    if(isValidHostname(hostname) == 0){
        herror("gethostbyname failed");
        exit(EXIT_FAILURE);
    }
    server_info = gethostbyname(hostname);
    if (!server_info) {
        herror("gethostbyname failed");
        exit(EXIT_FAILURE);
    }
    char *directory_path;
    directory_path = malloc(strlen(hostname) + 1);
    if (directory_path == NULL) {
        perror("Failed to allocate memory for directory_path\n");
        exit(EXIT_FAILURE);
    }
    snprintf(directory_path, strlen(hostname) + 1, "%s", hostname);

    // Check if the file exists locally by trying to open it
   if (handleLocalFile(hostname, sub_path) == -1) {
        // Create directories based on the slashes in sub_path
        char *token = strtok(sub_path, "/");
        char* path = malloc(INITIAL_SIZE);
        if (!path) {
            perror("Malloc failed");
            free(directory_path);
            exit(EXIT_FAILURE);
        }
        while (token != NULL) {
            if(strcmp(token,file_name) == 0){
                break;
            }
            strcat(path, token);
            mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH); // Create directory
            strcat(path, "/");  // Append a slash for the next iteration
            token = strtok(NULL, "/");
        }

     // Create a socket with the address format of IPV4 over TCP
    int sockfd = -1;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        perror("Socket failed\n");
        free(directory_path);
        exit(EXIT_FAILURE);
    }
    
    // Initialize sockaddr_in structure
    // Set its attributes to 0 to avoid undefined behavior
    memset(&sock_info, 0, sizeof(struct sockaddr_in));

    // Set the type of the address to be IPV4
    sock_info.sin_family = AF_INET;

    // Set the socket's port
    sock_info.sin_port = htons(*port);

    // Set the socket's ip
    sock_info.sin_addr.s_addr = ((struct in_addr*)server_info->h_addr_list[0])->s_addr;

    // connect to the server
    if (connect(sockfd, (struct sockaddr*)&sock_info, sizeof(struct sockaddr_in)) == -1) {
        perror("Connect Failed");
        close(sockfd);
        free(path);
       free(directory_path);
        exit(EXIT_FAILURE);
    }
        
        // Open the file with the full path and filename
        char *full_path= malloc(strlen(path) + strlen(file_name) + 64);
        snprintf(full_path,(strlen(path) + strlen(file_name) + 64), "%s%s", path, file_name);
        char* file_path = strchr(full_path, '/');
        printf("full_path = %s\n", full_path);
        printf("file_path = %s\n", file_path);
        FILE *file = fopen(full_path, "wb");
        if (file == NULL) {
            perror("Error opening file for writing");
            close(sockfd);
            free(full_path);
            free(path);
            free(directory_path);
            exit(EXIT_FAILURE);
        }

        // Send the HTTP request
        if (sendHTTPRequest(sockfd, hostname, file_path) == -1) {
            fclose(file);
            free(full_path);
            free(path);
            close(sockfd);
            free(directory_path);
            exit(EXIT_FAILURE);
        }

        // Receive and save the response body
        char* buffer = malloc(INITIAL_SIZE);
        if (!buffer) {
            perror("Malloc failed\n");
            free(full_path);
            free(path);
            free(directory_path);
            exit(EXIT_FAILURE);
        }
        ssize_t bytesRead;
        int bitesRead=0;
        bool header_end_found = false;
        while ((bytesRead = recv(sockfd, buffer, INITIAL_SIZE, 0)) > 0) {
            bitesRead+=bytesRead;
            if (!header_end_found) {
                char* body = strstr(buffer, "\r\n\r\n");
                if (body) {
                    // Check the HTTP status code
                    if (strstr(buffer, "404 Not Found")) {
                        fprintf(stderr, "Error: 404 Not Found\n");
                        fclose(file);
                        free(buffer);
                        close(sockfd);
                        free(path);
                        free(full_path);
                        free(directory_path);
                        exit(EXIT_FAILURE);
                    }
                    *body = '\0'; // Temporarily null-terminate here to print the header
                    printf("%s", buffer);

                    body += 4;  // Skip the "\r\n\r\n"
                    printf("%s", body);

                    fwrite(body, 1, bytesRead - (body - buffer), file);
                    header_end_found = true;
                        }
                    } else {
                        printf("%s", buffer);
                        fwrite(buffer, 1, bytesRead, file);
                    }
                    for(int i=0;i<INITIAL_SIZE;i++){
                        buffer[i]='\0';
                    }
        }
        printf("\nTotal response bytes: %d\n", bitesRead);
    if (bytesRead == -1) {
        perror("Error receiving data");
        fclose(file);
        free(buffer);
        free(directory_path);
        close(sockfd);
        free(full_path);
        free(path);
        exit(EXIT_FAILURE);
    }
    //Close everything
        free(buffer);
        fclose(file);
        free(full_path);
        free(path);
        free(directory_path);
        close(sockfd);
    }
}

void parseURL(char *url, char *hostname, char *sub_path, int *port) {
    // Skip the http:// or https:// part
    char *start = strstr(url, "://");
    if (start == NULL) {
        start = url;
    } else {
        start += 3;  // Skip past the ://
    }

    // Find the next slash
    const char *slash = strchr(start, '/');
    if (slash == NULL) {
        // The URL does not have a path part
        strcpy(sub_path, "/");
        slash = start + strlen(start);  // Point to the end of the string
    } else {
        // Copy the path part
        strcpy(sub_path, slash);
    }

    // If the path is just a slash, set it to /index.html
    if (strcmp(sub_path, "/") == 0) {
        strcpy(sub_path, "/index.html");
    }

    // Find the colon (for the port)
    const char *colon = strchr(start, ':');
if (colon != NULL && colon < slash) {
    // Copy the hostname part
    strncpy(hostname, start, colon - start);
    hostname[colon - start] = '\0';  // Add null terminator
    // Get the port number
    *port = atoi(colon + 1);
} else {
    // Copy the hostname part
    strncpy(hostname, start, slash - start);
    hostname[slash - start] = '\0';  // Add null terminator
    // If no port was specified, set it to 80
    *port = 80;
}
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc>3) {
        fprintf(stderr, "Usage: %s <URL> [-s]\n", argv[0]);
        exit(1);
    }
    char *url = argv[1];
    char *flag_s = (argc == 3 && strcmp(argv[2], "-s") == 0) ? "-s" : NULL;

    char hostname[1024];
    int port = 0;
    char sub_path[1024];
    // Parse the URL
    parseURL(url, hostname, sub_path, &port);
    char *filename = strrchr(sub_path, '/');
    if (filename != NULL) {
        filename++;  // Skip the slash
        if (*filename == '\0') {  // If we're at the end of the string
            // Find the previous slash
            *filename = '\0';  // Temporarily end the string here
            filename = strrchr(sub_path, '/');
            if (filename != NULL) {
                filename++;  // Skip the slash
            }
        }
    }
    char path[4096];
    snprintf(path, sizeof(path), "%s%s", hostname, sub_path);
    char *dup_path = strdup(path);
    if (!dup_path) {
        perror("strdup failed");
        exit(EXIT_FAILURE);
    }
    checker(hostname, path, &port, filename);
    if (flag_s) {
    size_t command_len = strlen("firefox ") + strlen(dup_path) + 1;
    char *command= malloc(command_len);
    snprintf(command, command_len, "firefox %s", dup_path);
    int result = system(command);
    if (result == -1) {
        perror("system call failed");
        free(command); // Free the memory before exiting
        free(dup_path);
    } else if (WEXITSTATUS(result) != 0) {
        fprintf(stderr, "firefox exited with status %d\n", WEXITSTATUS(result));
        free(command); // Free the memory before exiting
        free(dup_path);
        }
    }
  
    return 0;
}

