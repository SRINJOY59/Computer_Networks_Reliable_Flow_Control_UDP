

# KTP (Kernel Transport Protocol): Reliable Transport Protocol Implementation

## Overview

This repository contains an implementation of KTP (Kernel Transport Protocol), a reliable transport protocol built on top of UDP. The project was developed as part of the CS39006: Networks Laboratory course and demonstrates key concepts of transport layer reliability, flow control, and windowing mechanisms.

## Features

- **Reliable Data Transfer**: Implements sequence numbering, acknowledgments, and retransmission
- **Flow Control**: Sliding window protocol with dynamic window size adjustment
- **Error Handling**: Robust error detection and handling mechanisms
- **Socket API**: Provides a socket-like API similar to UDP but with reliability guarantees
- **Concurrent Connections**: Support for multiple simultaneous KTP sockets

## Protocol Specifications

- Fixed message size of 512 bytes
- 8-bit sequence numbers (1-255)
- Sender and receiver window management
- Duplicate detection and handling
- In-order delivery with buffering for out-of-order messages
- Timeout-based retransmission mechanism

## Architecture

The implementation consists of three main components:

1. **Socket Library**: Provides the KTP socket API (`ksocket`, `kbind`, `ksendto`, `krecvfrom`, `kclose`)
2. **Initialization Service**: Sets up shared memory, semaphores, and threads for KTP protocol management
3. **User Applications**: Sample client/server applications demonstrating file transfer over KTP

### Threading Model

- **Receiver Thread**: Handles incoming messages, acknowledgments, and window updates
- **Sender Thread**: Manages retransmissions and sending messages from the buffer
- **Garbage Collector**: Cleans up sockets from terminated processes

## Building the Project

```bash

make all

```

## Usage

### Starting the KTP Service

Before using any KTP sockets, you must start the initialization service:

```bash
./bin/initksocket
```

This service must remain running while any application uses KTP sockets.

### File Transfer Example

In one terminal:
```bash
# Start the sender
./bin/user1 127.0.0.1 8001 127.0.0.1 8002
```

In another terminal:
```bash
# Start the receiver
./bin/user2 127.0.0.1 8002 127.0.0.1 8001
```

This will transfer the contents of `testfile.txt` from the server to the client, which saves it as `received_file_port_8001.txt`.

## API Reference

### ksocket

```c
int ksocket(int domain, int type, int protocol);
```
Creates a KTP socket. Use `SOCK_KTP` as the type.

### kbind

```c
int kbind(int sockfd, const struct sockaddr *src_addr, socklen_t src_addrlen,const struct sockaddr *dest_addr, socklen_t dest_addrlen);
```
Binds a KTP socket with source and destination addresses.

### ksendto

```c
int ksendto(int sockfd, const void *buf, size_t len, int flags,const struct sockaddr *dest_addr, socklen_t dest_addrlen);
```
Sends data over a KTP socket with reliability guarantees.

### krecvfrom

```c
int krecvfrom(int sockfd, void *buf, size_t len, int flags,struct sockaddr *src_addr, socklen_t *src_addrlen);
```
Receives data from a KTP socket.

### kclose

```c
int kclose(int sockfd);
```
Closes a KTP socket and cleans up resources.

## Error Codes

- `ENOSPACE`: No space available in the buffer or no KTP socket available
- `ENOTBOUND`: Destination address doesn't match the bound address
- `ENOMESSAGE`: No message available in the receive buffer
- `EBADF`: Bad file descriptor or socket not active

## Limitations and Known Issues

- Currently supports IPv4 only
- Fixed message size of 512 bytes
- Sequence numbers limited to 8 bits (1-255)
- Maximum of 10 concurrent KTP sockets by default

## License

This project is for educational purposes as part of CS39006: Computer Networks Laboratory.

## Contributors

- [SRINJOY59](https://github.com/SRINJOY59)

---
