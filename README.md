# TCP-UDP Reliable Data Transfer

A reliable data transfer implementation over UDP using a custom stop-and-wait protocol with selective repeat. This project implements bidirectional communication between a client and server with chunked data transfer and acknowledgment mechanisms.

## Features

- Bidirectional communication (both client and server can send/receive)
- Message chunking with fixed chunk size (32 bytes)
- Reliable transfer with acknowledgment system
- Retransmission of lost packets
- Timeout handling
- Maximum retry attempts for failed transmissions

## Building the Project

Compile both client and server programs using gcc:

```bash
gcc -o server server.c
gcc -o client client.c