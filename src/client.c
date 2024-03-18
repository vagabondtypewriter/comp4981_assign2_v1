#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PORT_ARG "-p"
#define IP_ARG "-i"
#define ARG_COUNT 5
#define BASE_TEN 10
#define BUFFER_SIZE 1024

void parse_args(int argc, char *argv[], char **ip, uint16_t *port);
int  handle_args(const char *ip, uint16_t port);
void sigint_handler(int signum);
void handle_escape_sequence(const char *sequence);
void parse_ansi_sequences(const char *buffer);

static volatile sig_atomic_t exit_flag = 1;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

int main(int argc, char *argv[])
{
    char    *ip   = NULL;
    uint16_t port = 0;
    int      sock;
    parse_args(argc, argv, &ip, &port);

    sock = handle_args(ip, port);
    if(sock != -1)
    {
        ssize_t bytes_written;

        char buffer[BUFFER_SIZE];

        signal(SIGINT, sigint_handler);
        printf("New remote session started. Press <Ctrl+C>, then <Enter> to quit...\n");
        while(exit_flag == 1)
        {
            ssize_t bytes_read;
            printf("Enter your command: ");
            fflush(stdout);

            bytes_read = read(STDIN_FILENO, buffer, BUFFER_SIZE - 1);
            if(bytes_read == -1)
            {
                perror("Error reading from stdin");
                exit(EXIT_FAILURE);
            }
            else if(bytes_read == 0)
            {
                printf("0 bytes read. Exiting...\n");
                exit(EXIT_SUCCESS);
            }
            else
            {
                if(exit_flag == 0)
                {
                    printf("Exiting...\n");
                    return 0;
                }
                buffer[bytes_read] = '\0';
                printf("You entered: %s\n", buffer);

                // Send the input buffer to the server
                bytes_written = write(sock, buffer, strlen(buffer));
                if(bytes_written < 0)
                {
                    perror("Error writing to server");
                    exit(EXIT_FAILURE);
                }

                // Read response from server
                bytes_read = read(sock, buffer, BUFFER_SIZE - 1);
                if(bytes_read < 0)
                {
                    perror("Error reading from server");
                    exit(EXIT_FAILURE);
                }
                else if(bytes_read == 0)
                {
                    printf("Server closed connection\n");
                    break;
                }
                else
                {
                    buffer[bytes_read] = '\0';
                    parse_ansi_sequences(buffer);    // Parse ANSI escape sequences before printing
                }
            }
        }
    }

    return 0;
}

void parse_args(int argc, char *argv[], char **ip, uint16_t *port)
{
    int i;
    if(argc != ARG_COUNT)
    {
        fprintf(stderr, "Usage: %s %s <ip> %s <port>\n", argv[0], IP_ARG, PORT_ARG);
        exit(EXIT_FAILURE);
    }

    for(i = 1; i < argc; i += 2)
    {
        if(strcmp(argv[i], IP_ARG) == 0)
        {
            *ip = argv[i + 1];
        }
        else if(strcmp(argv[i], PORT_ARG) == 0)
        {
            char *endptr;
            long  tmp_port = strtol(argv[i + 1], &endptr, BASE_TEN);

            if(*endptr != '\0' || tmp_port < 0 || tmp_port > UINT16_MAX)
            {
                fprintf(stderr, "Invalid port number\n");
                exit(EXIT_FAILURE);
            }

            *port = (uint16_t)tmp_port;
        }
        else
        {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            exit(EXIT_FAILURE);
        }
    }
}

int handle_args(const char *ip, uint16_t port)
{
    int                sock;
    struct sockaddr_in serv_addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0)
    {
        perror("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(port);

    if(inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0)
    {
        perror("Invalid address/ Address not supported");
        close(sock);
        return -1;
    }

    if(connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Connection failed");
        close(sock);
        return -1;
    }

    printf("Connected to server\n");

    return sock;
}

void sigint_handler(int signum)
{
    (void)signum;
    exit_flag = 0;
}

void handle_escape_sequence(const char *sequence)
{
    // Handle different ANSI escape sequences here
    printf("Received escape sequence: %s\n", sequence);
}

void parse_ansi_sequences(const char *buffer)
{
    const size_t length = strlen(buffer);
    size_t       escape_length;
    char        *escape_sequence = (char *)malloc(length + 1);    // Allocate memory dynamically

    if(escape_sequence == NULL)
    {
        // Handle memory allocation failure
        fprintf(stderr, "Memory allocation failed\n");
        return;
    }

    escape_length = 0;

    for(size_t i = 0; i < length; i++)
    {
        if(buffer[i] == '\033')
        {    // Start of escape sequence
            escape_sequence[escape_length++] = buffer[i];
        }
        else if(escape_length > 0 && buffer[i] >= 'a' && buffer[i] <= 'z')
        {
            // End of escape sequence (a-z characters typically indicate the end)
            escape_sequence[escape_length++] = buffer[i];
            escape_sequence[escape_length]   = '\0';
            handle_escape_sequence(escape_sequence);
            escape_length = 0;    // Reset escape sequence buffer
        }
        else
        {
            printf("%c", buffer[i]);
        }
    }

    free(escape_sequence);    // Free dynamically allocated memory
}
