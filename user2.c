#include "ksocket.h"

#define BUFFER_SIZE 512

int main(int argc, char *argv[]) {
    if (argc != 5) {
        printf("Usage: %s <server_ip> <server_port> <client_ip> <client_port>\n", argv[0]);
        exit(1);
    }   
    int sockfd = ksocket(AF_INET, SOCK_KTP, 0);
    if (sockfd < 0) {
        perror("k_socket failed");
        exit(1);
    }    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);    
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(atoi(argv[4]));
    client_addr.sin_addr.s_addr = inet_addr(argv[3]);
    
    if (kbind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr),(struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
        perror("k_bind failed");
        exit(1);
    }
    
    char filename[64];
    snprintf(filename, sizeof(filename), "received_file_port_%s.txt", argv[4]);
    
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        file = fopen(filename, "wb");
        
        if (file == NULL) {
            perror("Failed to create file");
            kclose(sockfd);
            exit(1);
        }
    }
    
    char buffer[BUFFER_SIZE];
    int bytes_received;
    int total_received = 0;
    int retries = 0;
    
    while (1) {
        bytes_received = krecvfrom(sockfd, buffer, BUFFER_SIZE, 0, NULL, NULL);
        
        if (bytes_received < 0) {
            if (errno == ENOMESSAGE) {
                // No message available, wait and retry
                usleep(100000);  // 100ms
                retries++;
                
                if (retries > 100) {  // 10 seconds with no data
                    printf("No more data received for 10 seconds, assuming transmission complete\n");
                    break;
                }
                
                continue;
            } else {
                perror("k_recvfrom failed");
                break;
            }
        }        
        retries = 0;        
        fwrite(buffer, 1, bytes_received, file);
        total_received += bytes_received;
        printf("Received %d bytes, total: %d\n", bytes_received, total_received);
    }
    
    fclose(file);
    
    printf("File reception complete. Total bytes received: %d\n", total_received);
    printf("Data saved to file: %s\n", filename);
    
    kclose(sockfd);
    
    return 0;
}