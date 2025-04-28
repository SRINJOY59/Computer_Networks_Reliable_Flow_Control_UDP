#ifndef KSOCKET_H
#define KSOCKET_H

#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<sys/sem.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#define MAX_SOCKETS 10
#define DATA_MSG 1
#define ACK_MSG 2
#define SOCK_KTP 3
#define SHM_KEY 1234
#define SEM_KEY 5678
#define MAX_MSG_SIZE 512
#define MAX_WINDOW_SIZE 10
#define T 5
#define MAX_SEQ_NUM 256
#define DROP_PROB 0.2

#define ENOSPACE 100  // No space in buffer
#define ENOTBOUND 101  // KTP socket not bound
#define ENOMESSAGE 102  // No message in buffer

typedef struct {
    uint8_t type; //message type
    uint8_t seq_num;
    uint8_t last_ack;
    uint8_t window_size;
    char data[MAX_MSG_SIZE];
}ktp_message;

typedef struct{
    uint8_t base;
    uint8_t next_seq;
    uint8_t current_window_size; 
    uint8_t msg_seq_num[MAX_WINDOW_SIZE]; //seq number of each message
    time_t send_time[MAX_WINDOW_SIZE];
    ktp_message msg_buffer[MAX_WINDOW_SIZE];
}ktp_window;

typedef struct {
    int is_active;
    pid_t pid;
    int udp_socket;
    struct sockaddr_in src_addr;
    struct sockaddr_in dest_addr;
    ktp_window swnd;
    ktp_window rwnd;
    ktp_message send_buffer[MAX_WINDOW_SIZE];
    ktp_message recv_buffer[MAX_WINDOW_SIZE];
    int send_buffer_count;  //no of messages in send buffer
    int recv_buffer_count;  // no of messages in receiving buffer
    int nospace_flag;   //flag indicating if there is space in receiving buffer
}ktp_socket_info;

extern ktp_socket_info *socket_info;
extern int shm_id;
extern int sem_id;

int ksocket(int domain, int type, int protocol);
int kbind(int sockfd, const struct sockaddr *src_addr, socklen_t src_addrlen, const struct sockaddr *dest_addr, socklen_t dest_addrlen);
int ksendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t dest_addrlen);
int krecvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *src_addrlen);
int kclose(int sockfd);
void sem_lock();
void sem_unlock();
int drop_message(float probability);

#endif /* KSOCKET_H */