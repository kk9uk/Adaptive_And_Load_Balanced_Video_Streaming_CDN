#include "network_utils.h"

#include <string.h> // memcpy()

void increase_fd_limit() {
  struct rlimit curr;
  if (getrlimit(RLIMIT_NOFILE, &curr) == -1) {
    quick_exit(EXIT_FAILURE);
  }
  curr.rlim_max = MAX_NO_OF_PORTS;
  curr.rlim_cur = MAX_NO_OF_PORTS;
  if (setrlimit(RLIMIT_NOFILE, &curr) == -1) {
    quick_exit(EXIT_FAILURE);
  }
}

int get_outbound_socket(const char *const hostname, int port) {
  // (1) Create socket.
  int sockfd;
  if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
    quick_exit(EXIT_FAILURE);
  }

  // (2) Create a sockaddr_in to specify remote host and port.
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));

  // (2.1): specify socket family.
  // This is an IPv4 socket.
  addr.sin_family = AF_INET;

  // (2.2): specify remote socket address (hostname).
  // The socket will be a client, so call this unix helper function
  // to convert a hostname string to a useable `hostent` struct.
  struct hostent *host{};
  if ((host = gethostbyname(hostname)) == nullptr) {
    quick_exit(EXIT_FAILURE);
  }
  memcpy(&(addr.sin_addr), host->h_addr, host->h_length);

  // (2.3): Set the port value.
  // Use htons to convert from local byte order to network byte order.
  addr.sin_port = htons(static_cast<uint16_t>(port));

  // (3) Connect to remote server.
  if (connect(sockfd, (sockaddr *)&addr, sizeof(addr)) == -1) {
    quick_exit(EXIT_FAILURE);
  }

  return sockfd;
}

int get_inbound_socket(int port) {
  // (1) Create socket
  int sockfd;
  if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
    quick_exit(EXIT_FAILURE);
  }

  // (2) Set the "reuse port" socket option
  const int enable{1};
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) ==
      -1) {
    quick_exit(EXIT_FAILURE);
  }

  // (3) Create a sockaddr_in struct for the proper port and bind() to it.
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));

  // (3.1): specify socket family.
  // This is an IPv4 socket.
  addr.sin_family = AF_INET;

  // (3.2): specify client address whitelist (hostname).
  // The socket will be a server listening to everybody.
  addr.sin_addr.s_addr = INADDR_ANY;

  // (3.3): Set the port value.
  // If port is 0, the OS will choose the port for us.
  // Use htons to convert from local byte order to network byte order.
  addr.sin_port = htons(static_cast<uint16_t>(port));

  // (3.4) Bind to the port.
  if (bind(sockfd, (sockaddr *)&addr, sizeof(addr)) == -1) {
    quick_exit(EXIT_FAILURE);
  }

  // (4) Begin listening for clients.
  if (listen(sockfd, MAX_NO_OF_PORTS) == -1) {
    quick_exit(EXIT_FAILURE);
  }

  return sockfd;
}
