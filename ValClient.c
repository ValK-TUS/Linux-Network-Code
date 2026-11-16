/* Hangclient.c - Modernised Hangman Client */

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LINESIZE 80
#define HANGMAN_TCP_PORT "1066"

int main(int argc, char *argv[])
{
    int sock, count;
    char i_line[LINESIZE];
    char o_line[LINESIZE];
    char *server_name;

    struct addrinfo hints;
    struct addrinfo *result;
    int retcode;

    /* Get server name from command line.
       If none is provided, use localhost. */

    server_name = (argc > 1) ? argv[1] : "localhost";

    /* Initialise hints for getaddrinfo() */

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_UNSPEC;       /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;   /* TCP connection */

    /* Resolve server address */

    retcode = getaddrinfo(server_name, HANGMAN_TCP_PORT,
                          &hints, &result);

    if (retcode != 0)
    {
        fprintf(stderr, "getaddrinfo error: %s\n",
                gai_strerror(retcode));
        exit(1);
    }

    /* Create the socket */

    sock = socket(result->ai_family,
                  result->ai_socktype,
                  result->ai_protocol);

    if (sock < 0)
    {
        perror("Creating stream socket");
        freeaddrinfo(result);
        exit(1);
    }

    /* Connect to the server */

    if (connect(sock, result->ai_addr,
                result->ai_addrlen) < 0)
    {
        perror("Connecting to server");
        freeaddrinfo(result);
        close(sock);
        exit(1);
    }

    /* Release address information */

    freeaddrinfo(result);

    /* Successfully connected */

    printf("Connected to server %s\n", server_name);

    /* Communicate with the Hangman server */

    while ((count = read(sock, i_line, LINESIZE)) > 0)
    {
        /* Display message received from server */

        write(STDOUT_FILENO, i_line, count);

        /* Read user's guess/input */

        count = read(STDIN_FILENO, o_line, LINESIZE);

        if (count <= 0)
            break;

        /* Send user's input to server */

        if (write(sock, o_line, count) < 0)
        {
            perror("Writing to server");
            break;
        }
    }

    /* Close socket */

    close(sock);

    printf("\nDisconnected from server.\n");

    return 0;
}
