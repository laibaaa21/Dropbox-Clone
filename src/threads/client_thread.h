#ifndef CLIENT_THREAD_H
#define CLIENT_THREAD_H

/* Client thread function - handles connections from ClientQueue */
void *client_worker(void *arg);

#endif /* CLIENT_THREAD_H */
