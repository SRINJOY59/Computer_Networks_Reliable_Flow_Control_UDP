#include"ksocket.h"

ktp_socket_info *socket_info = NULL;
int shm_id = -1;
int sem_id = -1;

void init_shared_memory(){
    if(socket_info == NULL){
        shm_id = shmget(SHM_KEY, sizeof(ktp_socket_info)*MAX_SOCKETS, IPC_CREAT | 0666);
        if(shm_id < 0){
            perror("shmget failed");
            exit(1);
        }
        socket_info = (ktp_socket_info*)shmat(shm_id, NULL, 0);
        if(socket_info == (void*)-1){
            perror("shmat failed");
            exit(1);
        }

        sem_id = semget(SEM_KEY, 1, IPC_CREAT | 0666);
        if(sem_id < 0){
            perror("semget failed");
            exit(1);
        }
    }
}

void sem_lock(){
    struct sembuf sb = {0, -1, 0};
    if(semop(sem_id, &sb, 1) < 0){
        perror("semop failed");
        exit(1);
    }
}

void sem_unlock(){
    struct sembuf sb = {0, 1, 0};
    if(semop(sem_id, &sb, 1) < 0){
        perror("semop failed");
        exit(1);
    }
}

int drop_message(float prob){
    float r = ((float)rand()/(float)(RAND_MAX));
    if(r < prob){
        return 1;
    }
    return 0;
}

int ksocket(int domain, int type, int protocol){
    if(type != SOCK_KTP){
        errno = EINVAL;
        return -1;
    }

    init_shared_memory();

    sem_lock();
    int free_slot = -1;
    for(int i = 0;i<MAX_SOCKETS;i++){
        if(socket_info[i].is_active == 0){
            free_slot = i;
            break;
        }
    }
    if(free_slot == -1){
        sem_unlock();
        errno = ENOSPACE;
        return -1;
    }

    int udp_socket = socket(domain, SOCK_DGRAM, protocol);
    if(udp_socket < 0){
        perror("UDP socket creation failed");
        sem_unlock();
        return -1;
    }

    socket_info[free_slot].is_active = 1;
    socket_info[free_slot].pid = getpid();
    socket_info[free_slot].udp_socket = udp_socket;
    socket_info[free_slot].send_buffer_count = 0;
    socket_info[free_slot].recv_buffer_count = 0;
    socket_info[free_slot].nospace_flag = 0;

    socket_info[free_slot].swnd.base = 1;
    socket_info[free_slot].swnd.next_seq = 1;
    socket_info[free_slot].swnd.current_window_size = MAX_WINDOW_SIZE;

    socket_info[free_slot].rwnd.base = 1;
    socket_info[free_slot].rwnd.next_seq = 1;
    socket_info[free_slot].rwnd.current_window_size = MAX_WINDOW_SIZE;

    sem_unlock();
    return free_slot;
}

int kbind(int sockfd, const struct sockaddr *src_addr, socklen_t src_addrlen, const struct sockaddr *dest_addr, socklen_t dest_addrlen){
    if(sockfd < 0 || sockfd >= MAX_SOCKETS){
        errno = EBADF;
        return -1;
    }

    init_shared_memory();

    sem_lock();

    if(socket_info[sockfd].is_active == 0){
        sem_unlock();
        errno = EBADF;
        return -1;
    }

    memcpy(&socket_info[sockfd].src_addr, src_addr, src_addrlen);
    memcpy(&socket_info[sockfd].dest_addr, dest_addr, dest_addrlen);
    int res = bind(socket_info[sockfd].udp_socket, src_addr, src_addrlen);
    if(res < 0){
        perror("Binding failed!!");
        return -1;
    }

    sem_unlock();
    
    return res;
}

int ksendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t dest_addrlen){
    if(sockfd < 0 || sockfd >= MAX_SOCKETS){
        errno = EBADF;
        return -1;
    }

    init_shared_memory();
    
    sem_lock();

    if(socket_info[sockfd].is_active == 0){
        sem_unlock();
        errno = EBADF;
        return -1;
    }

    if(socket_info[sockfd].send_buffer_count >= MAX_WINDOW_SIZE){
        sem_unlock();
        errno = ENOSPACE;
        return -1;
    }

    struct sockaddr_in *dest = (struct sockaddr_in*)dest_addr;
    struct sockaddr_in *bound_dest = (struct sockaddr_in*)&socket_info[sockfd].dest_addr;
    
    if(dest->sin_addr.s_addr != bound_dest->sin_addr.s_addr || 
       dest->sin_port != bound_dest->sin_port){
        sem_unlock();
        errno = ENOTBOUND;
        return -1;
    }

    int buffer_idx = socket_info[sockfd].send_buffer_count;

    ktp_message *msg = &socket_info[sockfd].send_buffer[buffer_idx];
    msg->type = DATA_MSG;
    msg->seq_num = socket_info[sockfd].swnd.next_seq++;
    if(socket_info[sockfd].swnd.next_seq >= MAX_SEQ_NUM){
        socket_info[sockfd].swnd.next_seq = 1;
    }
    msg->last_ack = socket_info[sockfd].rwnd.base - 1;
    msg->window_size = socket_info[sockfd].rwnd.current_window_size;
    memcpy(msg->data, buf, len < MAX_MSG_SIZE ? len : MAX_MSG_SIZE);

    socket_info[sockfd].send_buffer_count++;

    sem_unlock();

    return len;
}

int krecvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *src_addrlen){
    if(sockfd < 0 || sockfd >= MAX_SOCKETS){
        errno = EBADF;
        return -1;
    }

    init_shared_memory();

    sem_lock();

    if(socket_info[sockfd].is_active == 0){
        sem_unlock();
        errno = EBADF;
        return -1;
    }
    if(socket_info[sockfd].recv_buffer_count == 0){
        sem_unlock();
        errno = ENOMESSAGE;
        return -1;
    }

    ktp_message *msg = &socket_info[sockfd].recv_buffer[0];

    memcpy(buf, msg->data, len < MAX_MSG_SIZE ? len : MAX_MSG_SIZE);

    for(int i = 0; i < socket_info[sockfd].recv_buffer_count - 1; i++){
        socket_info[sockfd].recv_buffer[i] = socket_info[sockfd].recv_buffer[i+1];
    }
    
    socket_info[sockfd].recv_buffer_count--;

    socket_info[sockfd].rwnd.current_window_size = MAX_WINDOW_SIZE - socket_info[sockfd].recv_buffer_count;

    if(socket_info[sockfd].rwnd.current_window_size > 0){
        socket_info[sockfd].nospace_flag = 0;
    }

    if(src_addr != NULL && src_addrlen != NULL){
        memcpy(src_addr, &socket_info[sockfd].src_addr, *src_addrlen);
    }

    sem_unlock();

    return len;
}

int kclose(int sockfd){
    if(sockfd < 0 || sockfd >= MAX_SOCKETS){
        errno = EBADF;
        return -1;
    }

    init_shared_memory();

    sem_lock();

    if(socket_info[sockfd].is_active == 0){
        sem_unlock();
        errno = EBADF;
        return -1;
    }

    close(socket_info[sockfd].udp_socket);
    socket_info[sockfd].is_active = 0;

    sem_unlock();

    return 0;
}