#include"ksocket.h"
#include<pthread.h>

void *receiver_thread(void *arg);
void *sender_thread(void *arg);
void garbage_collector();
int count_active_sockets();

int main(){
    printf("\n===== KTP Socket Initialization =====\n");
    printf("Creating shared memory segment...\n");
    
    shm_id = shmget(SHM_KEY, sizeof(ktp_socket_info)*MAX_SOCKETS, IPC_CREAT | 0666);
    if(shm_id < 0){
        perror("shmget failed");
        exit(1);
    }
    printf("Shared memory created with ID: %d\n", shm_id);

    socket_info = (ktp_socket_info*)shmat(shm_id, NULL, 0);
    if(socket_info == (void*)-1){
        perror("shmat failed");
        exit(1);
    }
    printf("Shared memory attached successfully\n");
    
    printf("Creating semaphore...\n");
    sem_id = semget(SEM_KEY, 1, IPC_CREAT | 0666);
    if(sem_id < 0){
        perror("semget failed");
        exit(1);
    }
    printf("Semaphore created with ID: %d\n", sem_id);
    
    memset(socket_info, 0, sizeof(ktp_socket_info)*MAX_SOCKETS);

    if(semctl(sem_id, 0, SETVAL, 1) < 0){
        perror("semctl failed");
        exit(1);
    }
    printf("Semaphore initialized\n");

    printf("Starting receiver thread...\n");
    pthread_t r_thread;
    pthread_t s_thread;
    pthread_t gc_thread;
    
    if(pthread_create(&r_thread, NULL, receiver_thread, NULL) != 0){
        perror("pthread_create failed for receiver");
        exit(1);
    }

    printf("Starting sender thread...\n");
    if(pthread_create(&s_thread, NULL, sender_thread, NULL) != 0){
        perror("pthread_create failed for sender");
        exit(1);
    }
    
    printf("Starting garbage collector thread...\n");
    if(pthread_create(&gc_thread, NULL, (void*)garbage_collector, NULL) != 0){
        perror("pthread_create failed for garbage collector");
        exit(1);
    }

    printf("\nKTP Socket system initialized and running!\n");
    printf("Maximum sockets: %d\n", MAX_SOCKETS);
    printf("Drop probability: %.2f\n", DROP_PROB);
    printf("Timeout: %d seconds\n", T);
    printf("======================================\n\n");
    
    while(1) {
        sleep(60);
        printf("KTP Socket system running... (%d active sockets)\n", count_active_sockets());
    }

    return 0;
}

int count_active_sockets() {
    int count = 0;
    sem_lock();
    for(int i = 0; i < MAX_SOCKETS; i++) {
        if(socket_info[i].is_active) count++;
    }
    sem_unlock();
    return count;
}

void *receiver_thread(void *arg) {
    printf("Receiver thread started\n");
    fd_set readfs;
    struct timeval tv;

    while(1) {
        FD_ZERO(&readfs);
        int maxfd = -1;
        int active_sockets = 0;
        int valid_fds[MAX_SOCKETS];
        int fd_to_socket_idx[FD_SETSIZE];
        
        memset(valid_fds, -1, sizeof(valid_fds));
        memset(fd_to_socket_idx, -1, sizeof(fd_to_socket_idx));
        
        sem_lock();
        for(int i = 0; i < MAX_SOCKETS; i++) {
            if(socket_info[i].is_active == 1 && socket_info[i].udp_socket >= 0) {
                int fd = socket_info[i].udp_socket;
                if(fd < FD_SETSIZE) { // Ensure FD is within limits
                    FD_SET(fd, &readfs);
                    valid_fds[i] = fd;
                    fd_to_socket_idx[fd] = i;
                    if(fd > maxfd) {
                        maxfd = fd;
                    }
                    active_sockets++;
                }
            }
        }
        sem_unlock();
        
        if(active_sockets == 0) {
            usleep(100000);  // 100ms
            continue;
        }

        tv.tv_sec = 5;
        tv.tv_usec = 0;

        int activity = select(maxfd + 1, &readfs, NULL, NULL, &tv);
        
        if(activity < 0) {
            if(errno == EINTR) continue;
            perror("select failed");
            usleep(100000);  // Add delay to prevent CPU-consuming tight loop
            continue;
        }

        if(activity == 0) {
            // Timeout occurred, nospace flag detection
            sem_lock();
            for(int i = 0; i < MAX_SOCKETS; i++) {
                if(socket_info[i].is_active == 1 && socket_info[i].nospace_flag == 1 && socket_info[i].udp_socket >= 0) {
                    int free_space = MAX_WINDOW_SIZE - socket_info[i].recv_buffer_count;
                    if(free_space > 0) {
                        ktp_message ack_msg;
                        memset(&ack_msg, 0, sizeof(ack_msg));
                        ack_msg.type = ACK_MSG;
                        ack_msg.seq_num = 0; 
                        ack_msg.last_ack = socket_info[i].rwnd.base - 1;
                        ack_msg.window_size = free_space;

                        int sent = sendto(socket_info[i].udp_socket, &ack_msg, sizeof(ack_msg), 0, (struct sockaddr*)&socket_info[i].dest_addr, sizeof(socket_info[i].dest_addr));
                        
                        if(sent < 0) {
                            if(errno != EBADF) { // Ignore EBADF errors, they're expected for closed sockets
                                perror("sendto failed in space notification");
                            }
                        } else {
                            printf("Space available notification sent (socket %d, window: %d)\n", i, free_space);
                            socket_info[i].nospace_flag = 0;  // Reset flag
                            socket_info[i].rwnd.current_window_size = free_space;  // Update window size
                        }
                    }
                }
            }
            sem_unlock();
            continue;
        }

        // Check which socket has data
        for(int i = 0; i < MAX_SOCKETS; i++) {
            int fd = valid_fds[i];
            if(fd == -1) continue;
            
            if(FD_ISSET(fd, &readfs)) {
                ktp_message msg;
                struct sockaddr_in src_addr;
                socklen_t addr_len = sizeof(src_addr);
                
                int len = recvfrom(fd, &msg, sizeof(msg), 0, (struct sockaddr*)&src_addr, &addr_len);
                
                if(len < 0) {
                    if(errno == EBADF) { // Socket was closed
                        // This socket is no longer valid, let's skip it
                        continue;
                    }
                    perror("recvfrom failed");
                    continue;
                }

                if(drop_message(DROP_PROB)) {
                    printf("Socket %d: Message dropped (type: %d, seq: %d)\n", i, msg.type, msg.seq_num);
                    continue;
                }

                sem_lock();                  
                if(socket_info[i].is_active != 1 || socket_info[i].udp_socket != fd) {
                    sem_unlock();
                    continue;
                }
                
                if(msg.type == DATA_MSG) {
                    printf("Socket %d: Received DATA (seq: %d)\n", i, msg.seq_num);
                    
                    int is_duplicate = 0;
                    for(int j = 0; j < MAX_WINDOW_SIZE; j++){
                        if(socket_info[i].rwnd.msg_seq_num[j] == msg.seq_num && msg.seq_num != 0){
                            is_duplicate = 1;
                            break;
                        }
                    }
                    
                    if(is_duplicate){
                        printf("Socket %d: Duplicate message received (seq: %d)\n", i, msg.seq_num);
                    } 
                    // if message is in the window range
                    else if(msg.seq_num >= socket_info[i].rwnd.base && 
                            msg.seq_num < socket_info[i].rwnd.base + socket_info[i].rwnd.current_window_size){
                        
                        if(socket_info[i].recv_buffer_count < MAX_WINDOW_SIZE){
                            socket_info[i].recv_buffer[socket_info[i].recv_buffer_count] = msg;
                            socket_info[i].recv_buffer_count++;

                            // Store sequence number in rwnd for duplicate detection
                            for(int j = 0; j < MAX_WINDOW_SIZE; j++) {
                                if(socket_info[i].rwnd.msg_seq_num[j] == 0) {
                                    socket_info[i].rwnd.msg_seq_num[j] = msg.seq_num;
                                    break;
                                }
                            }

                            //update window size
                            socket_info[i].rwnd.current_window_size = MAX_WINDOW_SIZE - socket_info[i].recv_buffer_count;
                            if(socket_info[i].rwnd.current_window_size == 0){
                                socket_info[i].nospace_flag = 1;
                                printf("Socket %d: Receive buffer full, setting nospace flag\n", i);
                            }
                            
                            //in order message
                            if(msg.seq_num == socket_info[i].rwnd.base){
                                socket_info[i].rwnd.base++;
                                if(socket_info[i].rwnd.base == MAX_SEQ_NUM){
                                    socket_info[i].rwnd.base = 1;
                                }

                                ktp_message ack_msg;
                                memset(&ack_msg, 0, sizeof(ack_msg));
                                ack_msg.type = ACK_MSG;
                                ack_msg.seq_num = 0;
                                ack_msg.last_ack = socket_info[i].rwnd.base - 1;
                                ack_msg.window_size = socket_info[i].rwnd.current_window_size;

                                int sent = sendto(socket_info[i].udp_socket, &ack_msg, sizeof(ack_msg), 0, (struct sockaddr*)&socket_info[i].dest_addr, sizeof(socket_info[i].dest_addr));
                                               
                                if(sent < 0) {
                                    if(errno != EBADF) {
                                        perror("sendto failed for ACK");
                                    }
                                } else {
                                    printf("Socket %d: Sent ACK (last_ack: %d, window: %d)\n", i, ack_msg.last_ack, ack_msg.window_size);
                                }

                                //sending acks to remaining in order message in receiver buffer (out of order message handling)
                                int j;
                                uint8_t next_expected_seq = socket_info[i].rwnd.base;
                                int additional_ack = 0;
                                while(1){
                                    int found = 0;
                                    for(j = 0; j < socket_info[i].recv_buffer_count; j++){
                                        if(socket_info[i].recv_buffer[j].seq_num == next_expected_seq){
                                            found = 1;
                                            next_expected_seq++;
                                            if(next_expected_seq == MAX_SEQ_NUM){
                                                next_expected_seq = 1;
                                            }
                                            additional_ack = 1;
                                            break;
                                        }
                                    }
                                    if(!found){
                                        break;
                                    }
                                }
                                
                                if(additional_ack){
                                    socket_info[i].rwnd.base = next_expected_seq;
                                    ack_msg.last_ack = next_expected_seq - 1;
                                    sent = sendto(socket_info[i].udp_socket, &ack_msg, sizeof(ack_msg), 0, (struct sockaddr*)&socket_info[i].dest_addr, sizeof(socket_info[i].dest_addr));
                                    if(sent >= 0) {
                                        printf("Socket %d: Sent additional ACK for ordered messages (last_ack: %d)\n", i, ack_msg.last_ack);
                                    }
                                }
                            }
                            else{
                                printf("Socket %d: Out of order message received (seq: %d, expected: %d)\n", i, msg.seq_num, socket_info[i].rwnd.base);
                            }
                        }
                        else{
                            printf("Socket %d: Receive buffer full, dropping message (seq: %d)\n", i, msg.seq_num);   
                        }
                    }
                    else{
                        printf("Socket %d: Message outside window (seq: %d, window base: %d, size: %d)\n", i, msg.seq_num, socket_info[i].rwnd.base, socket_info[i].rwnd.current_window_size);
                    }
                } 
                else if(msg.type == ACK_MSG) {
                    printf("Socket %d: Received ACK (last_ack: %d, window: %d)\n", i, msg.last_ack, msg.window_size);
                    
                    socket_info[i].swnd.base = (msg.last_ack + 1) % MAX_SEQ_NUM;
                    if(socket_info[i].swnd.base == 0) socket_info[i].swnd.base = 1;  // Ensure base is never 0
                    socket_info[i].swnd.current_window_size = msg.window_size;
                    
                    int j = 0;
                    while(j < MAX_WINDOW_SIZE) {
                        uint8_t seq = socket_info[i].swnd.msg_seq_num[j];
                        // If message sequence is before the new base, remove it
                        if(seq != 0 && ((seq <= msg.last_ack) || 
                           (msg.last_ack < socket_info[i].swnd.base && seq > socket_info[i].swnd.base))) {
                            socket_info[i].swnd.msg_seq_num[j] = 0;
                            printf("Socket %d: Message seq %d acknowledged\n", i, seq);
                        }
                        j++;
                    }
                }
                sem_unlock();
            }
        }
    }
    return NULL;
}

void *sender_thread(void *arg){
    printf("Sender thread started\n");
    
    while(1){
        // Sleep for T/2 seconds
        struct timespec ts;
        ts.tv_sec = T/2;
        ts.tv_nsec = (T % 2) * 500000000;
        nanosleep(&ts, NULL);
        
        sem_lock();
        // Check for timeouts and retransmit if needed
        time_t current_time = time(NULL);
        
        for(int i = 0; i < MAX_SOCKETS; i++){
            if(socket_info[i].is_active && socket_info[i].udp_socket >= 0){
                ktp_window *swnd = &socket_info[i].swnd;
                int retransmit = 0;
                
                for(int j = 0; j < MAX_WINDOW_SIZE; j++){
                    if(swnd->msg_seq_num[j] != 0 && current_time - swnd->send_time[j] >= T){
                        retransmit = 1;
                        break;
                    }
                }
                
                if(retransmit) {
                    printf("Socket %d: Timeout detected, retransmitting window\n", i);                    
                    int sock_fd = socket_info[i].udp_socket;
                    if(sock_fd < 0) continue;
                    
                    // Retransmission of all messages within that window
                    for(int k = 0; k < MAX_WINDOW_SIZE; k++){
                        if(swnd->msg_seq_num[k] != 0){
                            int sent = sendto(sock_fd, &swnd->msg_buffer[k], sizeof(ktp_message), 0, (struct sockaddr*)&socket_info[i].dest_addr, sizeof(socket_info[i].dest_addr));
                            
                            if(sent < 0) {
                                if(errno != EBADF) {
                                    perror("sendto failed in retransmission");
                                }
                            } else {
                                swnd->send_time[k] = current_time;
                                printf("Socket %d: Retransmitted message seq %d\n", i, swnd->msg_seq_num[k]);
                            }
                        }
                    }
                }
                
                if(socket_info[i].send_buffer_count > 0 && swnd->current_window_size > 0) {
                    printf("Socket %d: Processing send buffer (count: %d, window: %d)\n", i, socket_info[i].send_buffer_count, swnd->current_window_size);
                    
                    int sock_fd = socket_info[i].udp_socket;
                    if(sock_fd < 0) continue;
                    
                    // Process messages that can be sent within current window
                    int sent_count = 0;
                    int max_to_send = swnd->current_window_size;

                    // Send as many messages as we can within window constraints
                    for(int j = 0; j < socket_info[i].send_buffer_count && sent_count < max_to_send; j++) {
                        int free_slot = -1;
                        for(int k = 0; k < MAX_WINDOW_SIZE; k++) {
                            if(swnd->msg_seq_num[k] == 0) {
                                free_slot = k;
                                break;
                            }
                        }
                        
                        if(free_slot >= 0) {
                            memcpy(&swnd->msg_buffer[free_slot], &socket_info[i].send_buffer[j], sizeof(ktp_message));
                            swnd->msg_seq_num[free_slot] = socket_info[i].send_buffer[j].seq_num;
                            swnd->send_time[free_slot] = current_time;
                            
                            // Send the message
                            int sent = sendto(sock_fd, &socket_info[i].send_buffer[j], sizeof(ktp_message), 0, (struct sockaddr*)&socket_info[i].dest_addr, sizeof(socket_info[i].dest_addr));
                            if(sent < 0) {
                                if(errno != EBADF) {
                                    perror("sendto failed in sending from buffer");
                                }
                            } else {
                                printf("Socket %d: Sent message seq %d from buffer\n", i, socket_info[i].send_buffer[j].seq_num);
                                sent_count++;
                                
                                // Mark for removal from send_buffer
                                socket_info[i].send_buffer[j].seq_num = 0;
                            }
                        }
                    }

                    // Remove sent messages from send buffer
                    if(sent_count > 0) {
                        int new_count = 0;
                        for(int j = 0; j < socket_info[i].send_buffer_count; j++) {
                            if(socket_info[i].send_buffer[j].seq_num != 0) {
                                if(j != new_count) {
                                    memcpy(&socket_info[i].send_buffer[new_count], &socket_info[i].send_buffer[j], sizeof(ktp_message));
                                }
                                new_count++;
                            }
                        }
                        socket_info[i].send_buffer_count = new_count;
                        printf("Socket %d: Updated send buffer (new count: %d)\n", i, new_count);
                    }
                }
            }
        }
        sem_unlock();
    }
    return NULL;
}
void garbage_collector(){
    printf("Garbage collector thread started\n");
    
    while(1){
        sleep(5);
        
        sem_lock();
        for(int i = 0; i < MAX_SOCKETS; i++){
            if(socket_info[i].is_active){
                pid_t pid = socket_info[i].pid;                
                if(kill(pid, 0) == -1 && errno == ESRCH){
                    if(socket_info[i].udp_socket >= 0) {
                        close(socket_info[i].udp_socket);
                    }
                    socket_info[i].is_active = 0;
                    printf("Garbage collector: Cleaned up socket %d for dead process %d\n", i, pid);
                }
            }
        }
        sem_unlock();
    }
}