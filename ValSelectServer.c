/* Network server for Hangman game */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <ctype.h>

#define HANGMAN_TCP_PORT "1066"
#define MAXLEN 80
#define MAXLIVES 12

/* Maximum number of clients supported by select() */

#define MAXCLIENTS FD_SETSIZE

char *word[] = {
#include "words"
};

#define NUM_OF_WORDS (sizeof(word) / sizeof(word[0]))

/* Structure to store each client's Hangman game */

struct player
{
    int active;

    char *whole_word;
    char part_word[MAXLEN];

    int lives;
    int word_length;

    char input[MAXLEN];
    int input_length;
};

struct player players[MAXCLIENTS];


/* Send all data to a client */

int send_message(int fd, const char *message)
{
    size_t length = strlen(message);
    size_t sent = 0;

    while (sent < length)
    {
        ssize_t n = send(fd, message + sent,
                         length - sent, 0);

        if (n <= 0)
            return -1;

        sent += n;
    }

    return 0;
}


/* Start a new Hangman game for a client */

int start_game(int fd)
{
    char outbuf[MAXLEN + 100];
    char hostname[MAXLEN];

    gethostname(hostname, sizeof(hostname));

    players[fd].active = 1;

    players[fd].lives = MAXLIVES;

    players[fd].whole_word =
        word[rand() % NUM_OF_WORDS];

    players[fd].word_length =
        strlen(players[fd].whole_word);

    players[fd].input_length = 0;

    /* Hide the word initially */

    for (int i = 0; i < players[fd].word_length; i++)
    {
        players[fd].part_word[i] = '-';
    }

    players[fd].part_word[players[fd].word_length] = '\0';

    syslog(LOG_USER | LOG_INFO,
           "Server chose hangman word %s",
           players[fd].whole_word);

    /* Send welcome message */

    snprintf(outbuf, sizeof(outbuf),
             "Playing hangman on host %s:\n\n",
             hostname);

    if (send_message(fd, outbuf) < 0)
        return -1;

    /* Send initial word and lives */

    snprintf(outbuf, sizeof(outbuf),
             "%s %d\n",
             players[fd].part_word,
             players[fd].lives);

    if (send_message(fd, outbuf) < 0)
        return -1;

    return 0;
}


/* Process a player's guess */

int process_guess(int fd, char guess)
{
    char outbuf[MAXLEN + 100];

    int good_guess = 0;

    int i;

    /* Check whether the guessed letter is in the word */

    for (i = 0; i < players[fd].word_length; i++)
    {
        if (tolower((unsigned char)guess) ==
            tolower((unsigned char)players[fd].whole_word[i]))
        {
            good_guess = 1;

            players[fd].part_word[i] =
                players[fd].whole_word[i];
        }
    }

    /* Incorrect guess loses a life */

    if (!good_guess)
    {
        players[fd].lives--;
    }

    /* Check whether player has won */

    if (strcmp(players[fd].whole_word,
               players[fd].part_word) == 0)
    {
        snprintf(outbuf, sizeof(outbuf),
                 "%s %d\nYou won! Congratulations!\n",
                 players[fd].part_word,
                 players[fd].lives);

        send_message(fd, outbuf);

        return 1;
    }

    /* Check whether player has lost */

    if (players[fd].lives <= 0)
    {
        snprintf(outbuf, sizeof(outbuf),
                 "%s 0\nYou lost! The word was %s\n",
                 players[fd].whole_word,
                 players[fd].whole_word);

        send_message(fd, outbuf);

        return 1;
    }

    /* Send updated word and remaining lives */

    snprintf(outbuf, sizeof(outbuf),
             "%s %d\n",
             players[fd].part_word,
             players[fd].lives);

    if (send_message(fd, outbuf) < 0)
        return 1;

    return 0;
}


/* Main program */

int main(void)
{
    int welcome_s;
    int connect_s;

    int retcode;
    int max_fd;
    int fd;

    int option = 1;

    struct addrinfo hints;
    struct addrinfo *result;

    struct sockaddr_storage client_addr;

    socklen_t addr_len;

    fd_set current_sockets;
    fd_set ready_sockets;

    char *server_port = HANGMAN_TCP_PORT;

    srand((unsigned int)time(NULL));

    openlog("Hangman Server", LOG_PID, LOG_USER);

    /* Ignore SIGPIPE if a client disconnects unexpectedly */

    signal(SIGPIPE, SIG_IGN);

    /* ==========================================
       STEP 1: Obtain server address using
               getaddrinfo()
       ========================================== */

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET6;

    hints.ai_socktype = SOCK_STREAM;

    hints.ai_flags = AI_PASSIVE;

    retcode = getaddrinfo(NULL, server_port,
                          &hints, &result);

    if (retcode != 0)
    {
        fprintf(stderr, "getaddrinfo error: %s\n",
                gai_strerror(retcode));

        return 1;
    }

    /* ==========================================
       STEP 2: Create welcome socket
       ========================================== */

    welcome_s = socket(result->ai_family,
                       result->ai_socktype,
                       result->ai_protocol);

    if (welcome_s < 0)
    {
        perror("socket");
        freeaddrinfo(result);
        return 1;
    }

    /* Allow IPv4 and IPv6 connections where supported */

    retcode = setsockopt(welcome_s,
                         SOL_SOCKET,
                         SO_REUSEADDR,
                         &option,
                         sizeof(option));

    if (retcode < 0)
    {
        perror("setsockopt SO_REUSEADDR");
        close(welcome_s);
        freeaddrinfo(result);
        return 1;
    }

    option = 0;

    retcode = setsockopt(welcome_s,
                         IPPROTO_IPV6,
                         IPV6_V6ONLY,
                         &option,
                         sizeof(option));

    if (retcode < 0)
    {
        perror("setsockopt IPV6_V6ONLY");
        close(welcome_s);
        freeaddrinfo(result);
        return 1;
    }

    /* ==========================================
       STEP 3: Bind welcome socket
       ========================================== */

    retcode = bind(welcome_s,
                   result->ai_addr,
                   result->ai_addrlen);

    freeaddrinfo(result);

    if (retcode < 0)
    {
        perror("bind");
        close(welcome_s);
        return 1;
    }

    /* ==========================================
       STEP 4: Listen for incoming connections
       ========================================== */

    if (listen(welcome_s, 5) < 0)
    {
        perror("listen");
        close(welcome_s);
        return 1;
    }

    printf("Hangman server listening on port %s\n",
           HANGMAN_TCP_PORT);

    /* ==========================================
       STEP 5: Initialise socket sets
       ========================================== */

    FD_ZERO(&current_sockets);

    FD_SET(welcome_s, &current_sockets);

    max_fd = welcome_s;

    /* ==========================================
       STEP 6: FOREVER SERVER USING SELECT()
       ========================================== */

    while (1)
    {
        ready_sockets = current_sockets;

        retcode = select(max_fd + 1,
                         &ready_sockets,
                         NULL,
                         NULL,
                         NULL);

        if (retcode < 0)
        {
            if (errno == EINTR)
                continue;

            perror("select");
            continue;
        }

        /* Check all file descriptors */

        for (fd = 0; fd <= max_fd; fd++)
        {
            if (!FD_ISSET(fd, &ready_sockets))
                continue;

            /* ==================================
               NEW CLIENT CONNECTION
               ================================== */

            if (fd == welcome_s)
            {
                addr_len = sizeof(client_addr);

                connect_s = accept(welcome_s,
                                   (struct sockaddr *)&client_addr,
                                   &addr_len);

                if (connect_s < 0)
                {
                    perror("accept");
                    continue;
                }

                if (connect_s >= MAXCLIENTS)
                {
                    printf("Client rejected: too many connections.\n");
                    close(connect_s);
                    continue;
                }

                printf("Client connected. fd = %d\n",
                       connect_s);

                /* Add client to socket set */

                FD_SET(connect_s, &current_sockets);

                if (connect_s > max_fd)
                    max_fd = connect_s;

                /* Start a separate Hangman game */

                if (start_game(connect_s) < 0)
                {
                    close(connect_s);

                    FD_CLR(connect_s, &current_sockets);

                    players[connect_s].active = 0;
                }
            }

            /* ==================================
               EXISTING CLIENT CONNECTION
               ================================== */

            else
            {
                char inbuf[MAXLEN];

                retcode = recv(fd, inbuf,
                               sizeof(inbuf), 0);

                /* Client disconnected */

                if (retcode <= 0)
                {
                    if (retcode == 0)
                    {
                        printf("Client fd %d disconnected.\n",
                               fd);
                    }
                    else
                    {
                        perror("recv");
                    }

                    close(fd);

                    FD_CLR(fd, &current_sockets);

                    players[fd].active = 0;

                    continue;
                }

                /* ==================================
                   PROCESS CLIENT INPUT
                   ================================== */

                for (int i = 0; i < retcode; i++)
                {
                    char ch = inbuf[i];

                    if (ch == '\n')
                    {
                        /* Process a completed guess */

                        if (players[fd].input_length > 0)
                        {
                            char guess =
                                players[fd].input[0];

                            players[fd].input_length = 0;

                            if (process_guess(fd, guess))
                            {
                                printf("Game finished for client fd %d\n",
                                       fd);

                                close(fd);

                                FD_CLR(fd, &current_sockets);

                                players[fd].active = 0;

                                break;
                            }
                        }
                    }

                    else if (ch != '\r')
                    {
                        /* Store incoming characters */

                        if (players[fd].input_length < MAXLEN - 1)
                        {
                            players[fd].input[
                                players[fd].input_length++] = ch;
                        }
                    }
                }
            }
        }
    }

    /* Close welcome socket */

    close(welcome_s);

    closelog();

    return 0;
}

