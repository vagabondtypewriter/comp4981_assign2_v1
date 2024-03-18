#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define PORT_ARG "-p"
#define IP_ARG "-i"
#define REQUIRED_ARGS 5
#define MAX_CONN 5
#define BASE_TEN 10
#define BUFFER_SIZE 1024
#define PATH_MAX 1024
#define MAX_ARGUMENTS 10
#define BIN 6

static volatile sig_atomic_t exit_flag = 1;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static int                   server_fd;        // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void free_arguments(char **arguments);

static void parse_arguments(const char *buffer, char **command, char ***arguments)
{
    size_t buffer_length = strlen(buffer);
    int    i             = 0;
    int    arg_count     = 0;
    int    arg_start;
    int    arg_length;
    int    space_encountered = 0;
    char  *bin_command;

    *command = (char *)malloc(buffer_length + 1);
    if(*command == NULL)
    {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    while(buffer[i] != ' ' && buffer[i] != '\n' && buffer[i] != '\t' && buffer[i] != '\0')
    {
        (*command)[i] = buffer[i];
        i++;
    }
    (*command)[i] = '\0';

    // Append "/bin/" to the command
    bin_command = (char *)malloc(strlen(*command) + BIN);    // 6 is the length of "/bin/" plus null terminator
    if(bin_command == NULL)
    {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }
    sprintf(bin_command, "/bin/%s", *command);
    free(*command);    // Free the original command
    *command = bin_command;

    // Allocate memory for the arguments array with space for the maximum expected number of arguments
    *arguments = (char **)malloc((MAX_ARGUMENTS + 1) * sizeof(char *));
    if(*arguments == NULL)
    {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    // Initialize arguments array
    for(int j = 0; j <= MAX_ARGUMENTS; j++)
    {
        (*arguments)[j] = NULL;
    }

    // Extract arguments from the buffer
    while(buffer[i] != '\0')
    {
        // Skip whitespace characters
        while(buffer[i] == ' ' || buffer[i] == '\n' || buffer[i] == '\t')
        {
            i++;
            space_encountered = 1;
            if(buffer[i] == '\0')
            {
                break;
            }
        }

        if(space_encountered)
        {
            space_encountered = 0;
        }
        else
        {
            // Store the start index of the argument
            arg_start = i;

            // Find the end of the argument
            while(buffer[i] != ' ' && buffer[i] != '\n' && buffer[i] != '\t' && buffer[i] != '\0')
            {
                i++;
            }

            // Calculate the length of the argument
            arg_length = i - arg_start;

            // Allocate memory for the argument string
            (*arguments)[arg_count] = (char *)malloc((size_t)arg_length + 1);
            if((*arguments)[arg_count] == NULL)
            {
                perror("Memory allocation failed");
                exit(EXIT_FAILURE);
            }

            // Copy the argument from the buffer to the allocated memory
            for(int j = 0; j < arg_length; j++)
            {
                (*arguments)[arg_count][j] = buffer[arg_start + j];
            }
            (*arguments)[arg_count][arg_length] = '\0';    // Null-terminate the argument string

            // Move to the next argument
            arg_count++;

            // Check if the number of arguments exceeds the maximum expected
            if(arg_count > MAX_ARGUMENTS)
            {
                fprintf(stderr, "Exceeded maximum number of arguments\n");
                break;
            }
        }
    }
}

void free_arguments(char **arguments)
{
    if(arguments == NULL)
    {
        return;
    }

    for(int i = 0; arguments[i] != NULL; i++)
    {
        free(arguments[i]);
    }
    free(arguments);
}

typedef struct
{
    char cwd[PATH_MAX];    // Current working directory
    // Add other session-specific data as needed
} Session;

static void handle_client(int client_socket, Session *session)
{
    char    buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    while((bytes_read = read(client_socket, buffer, sizeof(buffer))) > 0)
    {
        char  *command;
        char **arguments;
        buffer[bytes_read] = '\0';         // Null-terminate the received data
        printf("Received: %s", buffer);    // Print the received data

        // Parse command and arguments
        parse_arguments(buffer, &command, &arguments);
        printf("Command: %s\n", command);
        if(strcmp(command, "/bin/cd") == 0)
        {
            printf("args[0] = %s\n", arguments[0]);
            if(chdir(arguments[0]) == 0)
            {
                if(getcwd(session->cwd, sizeof(session->cwd)) == NULL)
                {
                    perror("getcwd");
                }
                else
                {
                    printf("Writing to client:\n");
                    write(client_socket, session->cwd, strlen(session->cwd));
                }
            }
            else
            {
                perror("chdir");
            }
        }
        else if(strncmp(command, "exit", 4) == 0)
        {
            printf("Exiting...\n");
            break;    // Exit loop and close connection
        }
        else
        {
            // Execute command
            pid_t pid = fork();
            if(pid < 0)
            {
                perror("fork failed");
            }
            else if(pid == 0)
            {
                // Child process
                dup2(client_socket, STDOUT_FILENO);    // Redirect stdout to client socket
                execvp(command, arguments);
                printf("exec failed: unrecognized command\n");
                exit(EXIT_FAILURE);
            }
            else
            {
                // Parent process
                waitpid(pid, NULL, 0);    // Wait for child process to finish
            }
        }

        free(command);
        free_arguments(arguments);
    }

    if(bytes_read == 0)
    {
        printf("Client disconnected\n");
    }
    else
    {
        perror("Error reading from client");
    }

    close(client_socket);
}

static void sigint_handler(int signum)
{
    (void)signum;
    exit_flag = 0;
    close(server_fd);
}

int main(int argc, const char *argv[])
{
    struct sockaddr_in address;
    int                opt     = 1;
    int                addrlen = sizeof(address);
    char              *endptr;
    long               port;
    const char        *ip;

    if(argc != REQUIRED_ARGS || strcmp(argv[1], PORT_ARG) != 0 || strcmp(argv[3], IP_ARG) != 0)
    {
        fprintf(stderr, "Usage: %s %s <port> %s <ip>\n", argv[0], PORT_ARG, IP_ARG);
        exit(EXIT_FAILURE);
    }

    port = strtol(argv[2], &endptr, BASE_TEN);

    if(*endptr != '\0' || port < 0 || port > UINT16_MAX)
    {
        printf("Invalid port number\n");
        exit(EXIT_FAILURE);
    }

    ip = argv[4];

    printf("Server\n");

    signal(SIGINT, sigint_handler);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = inet_addr(ip);
    address.sin_port        = htons((uint16_t)port);

    if(bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if(listen(server_fd, MAX_CONN) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on %s:%ld\n", ip, port);

    while(exit_flag == 1)
    {
        int     new_socket;
        pid_t   pid;
        Session session;

        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if(new_socket < 0)
        {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        printf("New connection accepted\n");

        if(getcwd(session.cwd, sizeof(session.cwd)) == NULL)
        {
            perror("getcwd");
            exit(EXIT_FAILURE);
        }

        pid = fork();
        if(pid < 0)
        {
            perror("fork failed");
            close(new_socket);
        }
        else if(pid == 0)
        {
            // Child process
            close(server_fd);
            handle_client(new_socket, &session);
            exit(0);
        }
        else
        {
            // Parent process
            close(new_socket);
        }
    }

    printf("Server shutting down...\n");
    close(server_fd);
    return 0;
}
