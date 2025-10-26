#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include <stddef.h>
#include <sys/types.h>

/**
 * recv_full - Receive exactly N bytes from a socket
 *
 * Handles partial reads and retries until exactly 'len' bytes are received
 * or an error/disconnect occurs.
 *
 * @param sockfd: Socket file descriptor
 * @param buffer: Buffer to store received data
 * @param len: Number of bytes to receive
 * @return: Number of bytes received (len on success, < len on error/disconnect)
 */
ssize_t recv_full(int sockfd, void *buffer, size_t len);

/**
 * send_full - Send exactly N bytes to a socket
 *
 * Handles partial writes and retries until exactly 'len' bytes are sent
 * or an error occurs.
 *
 * @param sockfd: Socket file descriptor
 * @param buffer: Buffer containing data to send
 * @param len: Number of bytes to send
 * @return: Number of bytes sent (len on success, < len on error)
 */
ssize_t send_full(int sockfd, const void *buffer, size_t len);

/**
 * send_error - Send an error message to client with proper error checking
 *
 * @param sockfd: Socket file descriptor
 * @param error_msg: Error message to send
 * @return: 0 on success, -1 on error
 */
int send_error(int sockfd, const char *error_msg);

/**
 * send_success - Send a success message to client with proper error checking
 *
 * @param sockfd: Socket file descriptor
 * @param success_msg: Success message to send
 * @return: 0 on success, -1 on error
 */
int send_success(int sockfd, const char *success_msg);

#endif /* NETWORK_UTILS_H */
