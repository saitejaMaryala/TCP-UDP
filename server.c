// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <stdbool.h>
#include <sys/time.h>

#define CHUNK_SIZE 32
#define TIMEOUT 0.1 // Timeout for ACK in seconds
#define MAX_CHUNKS 100
#define PORT 8080
#define MAX_RETRIES 5 // Max number of retransmission attempts per chunk

int sending_data = 0; // 0 for receiving, 1 for sending
struct packet
{
    int sequence_number;   // Sequence number of the chunk
    int total_chunks;      // Total number of chunks being sent
    char data[CHUNK_SIZE]; // Actual chunk data
};

struct ack_packet
{
    int sequence_number; // Sequence number being acknowledged
};

int ack_received[MAX_CHUNKS]; // Track received ACKs

void send_data(int sockfd, struct sockaddr_in *server_addr, char *message) {
    struct packet pkt;
    socklen_t addr_len = sizeof(*server_addr);
    int total_chunks = (strlen(message) + CHUNK_SIZE - 1) / CHUNK_SIZE; // Calculate total chunks

    // Send all chunks without waiting for ACKs
    for (int i = 0; i < total_chunks; i++) {
        // Prepare the packet
        strncpy(pkt.data, &message[i * CHUNK_SIZE], CHUNK_SIZE);
        pkt.sequence_number = i;
        pkt.total_chunks = total_chunks;

        // Send the packet
        sendto(sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr *)server_addr, addr_len);
        printf("Sent chunk %d\n", i);
    }

    // Handle retransmissions with a maximum retry count
    for (int i = 0; i < total_chunks; i++) {
        struct timeval start, end;
        int retries = 0;
        gettimeofday(&start, NULL);

        while (retries < MAX_RETRIES) {
            gettimeofday(&end, NULL);
            double elapsed_time = (end.tv_sec - start.tv_sec) + ((end.tv_usec - start.tv_usec) / 1000000.0);

            if (ack_received[i]) {
                break; // ACK received, move to next chunk
            }

            // If timeout, retransmit the chunk
            if (elapsed_time >= TIMEOUT) {
                retries++;
                printf("Timeout on chunk %d, resending (Attempt %d of %d)...\n", i, retries, MAX_RETRIES);
                strncpy(pkt.data, &message[i * CHUNK_SIZE], CHUNK_SIZE);
                pkt.sequence_number = i;
                sendto(sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr *)server_addr, addr_len);
                gettimeofday(&start, NULL); // Reset the timer after retransmission
            }

            // If maximum retries are reached, give up on the chunk
            if (retries == MAX_RETRIES) {
                printf("Max retries reached for chunk %d. Giving up on retransmission.\n", i);
                break;
            }

            // Non-blocking receive for ACKs
            fd_set read_fds;
            struct timeval tv;
            FD_ZERO(&read_fds);
            FD_SET(sockfd, &read_fds);
            tv.tv_sec = 0; // No wait
            tv.tv_usec = 100; // Check every 0.1 ms

            if (select(sockfd + 1, &read_fds, NULL, NULL, &tv) > 0) {
                struct ack_packet ack_pkt;
                struct sockaddr_in sender_addr;
                socklen_t addr_len = sizeof(sender_addr);
                
                if (recvfrom(sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&sender_addr, &addr_len) > 0) {
                    ack_received[ack_pkt.sequence_number] = 1; // Mark ACK as received
                    printf("ACK received for chunk %d\n", ack_pkt.sequence_number);
                }
            }
        }
    }
}

void receive_data(int sockfd)
{
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    struct packet pkt;
    struct ack_packet ack_pkt;
    char received_chunks[MAX_CHUNKS][CHUNK_SIZE + 1]; // To store received data
    int total_chunks = -1, chunk_count = 0;
    int ack_sent_flags[MAX_CHUNKS] = {0}; // Track which chunks have been acknowledged

    while (chunk_count < total_chunks || total_chunks == -1)
    {
        // Receive a packet
        if (recvfrom(sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&client_addr, &addr_len) > 0)
        {
            // Randomly skip sending the ACK (for testing retransmission)
            // if ((rand() % 10) < 7)
            // {
                // If ACK hasn't been sent for this chunk yet
                if (ack_sent_flags[pkt.sequence_number] == 0)
                {
                    // Store the chunk only if the ACK is being sent for the first time
                    strncpy(received_chunks[pkt.sequence_number], pkt.data, CHUNK_SIZE);
                    ack_sent_flags[pkt.sequence_number] = 1; // Mark ACK as sent
                    received_chunks[pkt.sequence_number][CHUNK_SIZE] = '\0';
                    chunk_count++;
                    printf("Received and stored chunk %d: %s\n", pkt.sequence_number, received_chunks[pkt.sequence_number]);
                }
                else
                {
                    // Handle retransmitted chunk where ACK was skipped previously
                    printf("Retransmitted chunk %d received and storing now.\n", pkt.sequence_number);
                }

                // Send ACK for this chunk
                ack_pkt.sequence_number = pkt.sequence_number;
                sendto(sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&client_addr, addr_len);
                printf("Sent ACK for chunk %d\n", pkt.sequence_number);
            // }
            // else
            // {
            //     // If ACK is skipped, don't store the chunk and leave it for retransmission
            //     printf("Skipped ACK for chunk %d, not storing it!\n", pkt.sequence_number);
            // }

            // Set the total number of chunks after receiving the first chunk
            if (total_chunks == -1)
            {
                total_chunks = pkt.total_chunks;
            }
        }
    }

    // Reassemble and print the message
     printf("All chunks received. Assembling message:\n");
    for (int i = 0; i < total_chunks; i++)
    {
        if (ack_sent_flags[i]) // Only print acknowledged chunks
        {
            printf("%s", received_chunks[i]);
        }
    }
    printf("\n");
}

int main()
{
    int sockfd;
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Server address configuration
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind the socket to the server address
    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);
    memset(ack_received, 0, sizeof(ack_received)); // Initialize all ACKs as not received

    int temp;

    struct sockaddr_in client_addr;
    socklen_t caddr_len = sizeof(client_addr);

    recvfrom(sockfd, &temp, sizeof(temp), 0, (struct sockaddr *)&client_addr, &caddr_len);
    printf("temp:%d\n", temp);

    while (1)
    {
        if (!sending_data)
        {
            receive_data(sockfd);
            sending_data = 1;
        }
        else
        {
            char msg[1024];
            printf("Enter message: ");
            fgets(msg, sizeof(msg), stdin);
            memset(ack_received, 0, sizeof(ack_received));
            send_data(sockfd, &client_addr, msg);
            sending_data = 0;
        }
    }

    close(sockfd);
    return 0;
}
