#include"ksocket.h"

#define BUFFER_SIZE 512

int main(int argc, char *argv[]){
    if (argc != 5) {
        printf("Usage: %s <server_ip> <server_port> <client_ip> <client_port>\n", argv[0]);
        exit(1);
    } 
    int sockfd = ksocket(AF_INET, SOCK_KTP, 0);
    if(sockfd < 0){
        perror("Socket creation failed!!\n");
        return -1;
    }
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    server_addr.sin_port = htons(atoi(argv[2]));

    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = inet_addr(argv[3]);
    client_addr.sin_port = htons(atoi(argv[4]));

    if (kbind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr),(struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
        perror("k_bind failed");
        exit(1);
    }

    printf("Server is running on port %s\n", argv[2]);
    FILE *file = fopen("testfile.txt", "rb");
    if (file == NULL) {
        perror("Failed to open file");
        exit(1);
    }

    char buffer[BUFFER_SIZE];
    int bytes_read = 0;
    int total_sent = 0;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        if (ksendto(sockfd, buffer, bytes_read, 0,(struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
            if (errno == ENOSPACE) {
                // Buffer full, wait and retry
                usleep(100000);  // 100ms
                continue;
            } else {
                perror("k_sendto failed");
                break;
            }
        }
        
        total_sent += bytes_read;
        printf("Sent %d bytes, total: %d\n", bytes_read, total_sent);
        usleep(10000);  // 10ms
    }

    fclose(file);
    
    printf("File sending complete. Total bytes sent: %d\n", total_sent);    
    sleep(10);
    kclose(sockfd);
    
    return 0;
}