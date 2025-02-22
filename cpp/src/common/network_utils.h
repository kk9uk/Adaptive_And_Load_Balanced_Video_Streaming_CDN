#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include <arpa/inet.h> // htons(), ntohs()
#include <cstdlib>
#include <netdb.h>      // gethostbyname(), struct hostent
#include <netinet/in.h> // struct sockaddr_in
#include <sys/resource.h>

#define MAX_NO_OF_PORTS 65536

void increase_fd_limit();

int get_outbound_socket(const char *const hostname, int port);

int get_inbound_socket(int port);

#endif // NETWORK_UTILS_H
