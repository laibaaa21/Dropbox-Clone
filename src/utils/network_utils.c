#include "network_utils.h"
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/**
 * recv_full - Receive exactly N bytes from a socket
 *
 * Handles partial reads by looping until all requested bytes are received.
 * Returns early if the connection is closed or an unrecoverable error occurs.
 */
ssize_t recv_full(int sockfd, void *buffer, size_t len)
{
    if (!buffer || len == 0)
    {
        errno = EINVAL;
        return -1;
    }

    size_t total_received = 0;
    char *buf = (char *)buffer;

    while (total_received < len)
    {
        ssize_t n = recv(sockfd, buf + total_received, len - total_received, 0);

        if (n < 0)
        {
            /* Error occurred */
            if (errno == EINTR)
            {
                /* Interrupted by signal, retry */
                continue;
            }
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                /* Would block on non-blocking socket, retry */
                continue;
            }
            else
            {
                /* Unrecoverable error */
                perror("[NetworkUtils] recv_full error");
                return total_received;
            }
        }
        else if (n == 0)
        {
            /* Connection closed by peer */
            return total_received;
        }

        total_received += n;
    }

    return (ssize_t)total_received;
}

/**
 * send_full - Send exactly N bytes to a socket
 *
 * Handles partial writes by looping until all requested bytes are sent.
 * Returns early if an unrecoverable error occurs.
 */
ssize_t send_full(int sockfd, const void *buffer, size_t len)
{
    if (!buffer || len == 0)
    {
        errno = EINVAL;
        return -1;
    }

    size_t total_sent = 0;
    const char *buf = (const char *)buffer;

    while (total_sent < len)
    {
        ssize_t n = send(sockfd, buf + total_sent, len - total_sent, 0);

        if (n < 0)
        {
            /* Error occurred */
            if (errno == EINTR)
            {
                /* Interrupted by signal, retry */
                continue;
            }
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                /* Would block on non-blocking socket, retry */
                continue;
            }
            else if (errno == EPIPE)
            {
                /* Broken pipe - client disconnected */
                fprintf(stderr, "[NetworkUtils] send_full: broken pipe (client disconnected)\n");
                return total_sent;
            }
            else
            {
                /* Unrecoverable error */
                perror("[NetworkUtils] send_full error");
                return total_sent;
            }
        }

        total_sent += n;
    }

    return (ssize_t)total_sent;
}

/**
 * send_error - Send an error message to client with proper error checking
 */
int send_error(int sockfd, const char *error_msg)
{
    if (!error_msg)
    {
        errno = EINVAL;
        return -1;
    }

    size_t len = strlen(error_msg);
    ssize_t sent = send_full(sockfd, error_msg, len);

    if (sent != (ssize_t)len)
    {
        fprintf(stderr, "[NetworkUtils] send_error: failed to send complete error message\n");
        return -1;
    }

    return 0;
}

/**
 * send_success - Send a success message to client with proper error checking
 */
int send_success(int sockfd, const char *success_msg)
{
    if (!success_msg)
    {
        errno = EINVAL;
        return -1;
    }

    size_t len = strlen(success_msg);
    ssize_t sent = send_full(sockfd, success_msg, len);

    if (sent != (ssize_t)len)
    {
        fprintf(stderr, "[NetworkUtils] send_success: failed to send complete success message\n");
        return -1;
    }

    return 0;
}
