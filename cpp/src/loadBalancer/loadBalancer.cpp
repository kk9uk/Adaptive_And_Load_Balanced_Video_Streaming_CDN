#include "djikstra.h"
#include "loadBalancer_protocol.h"
#include "network_utils.h"
#include "spdlog/spdlog.h"
#include <arpa/inet.h>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cxxopts.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  increase_fd_limit();

  cxxopts::Options cxxopts{"loadBalancer",
                           "A simple load balancer that implements round-robin "
                           "& geographic load balancing."};
  cxxopts.add_options()("p,port", "Port of the load balancer",
                        cxxopts::value<int>())(

      "g,geo", "Run in geo mode",
      cxxopts::value<bool>()->default_value("false"))(

      "r,rr", "Run in round-robin mode",
      cxxopts::value<bool>()->default_value("false"))(

      "s,servers", "Path to file containing server info",
      cxxopts::value<std::string>());

  int loadBalancer_port;
  bool is_geo, is_rr;
  std::string server_info_path;
  try {
    const auto cxxopts_argv{cxxopts.parse(argc, argv)};
    loadBalancer_port = cxxopts_argv["port"].as<int>();
    is_geo = cxxopts_argv["geo"].as<bool>();
    is_rr = cxxopts_argv["rr"].as<bool>();
    server_info_path = cxxopts_argv["servers"].as<std::string>();
  } catch (const cxxopts::exceptions::parsing &e) {
    return EXIT_FAILURE;
  }
  if (1024 > loadBalancer_port || loadBalancer_port > 65535) {
    return EXIT_FAILURE;
  } else if ((is_geo && is_rr) || (!is_geo && !is_rr)) {
    return EXIT_FAILURE;
  }

  if (is_rr) {
    FILE *server_info_file;
    if ((server_info_file = fopen(server_info_path.c_str(), "r")) == NULL) {
      return EXIT_FAILURE;
    }

    int num_servers;
    if (fscanf(server_info_file, "%*s%d", &num_servers) != 1) {
      return EXIT_FAILURE;
    }
    char ip_str[16];
    in_addr ip_addr;
    in_addr_t server_ips[num_servers];
    unsigned short server_ports[num_servers];
    for (int i = 0; i < num_servers; ++i) {
      if (fscanf(server_info_file, "%s%hu", ip_str, &server_ports[i]) != 2) {
        return EXIT_FAILURE;
      }
      if (inet_pton(AF_INET, ip_str, &ip_addr) != 1) {
        return EXIT_FAILURE;
      }
      server_ips[i] = ip_addr.s_addr;
    }

    if (fclose(server_info_file) == EOF) {
      return EXIT_FAILURE;
    }

    // (1) Create socket
    int loadBalancer_fd;
    if ((loadBalancer_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
      return EXIT_FAILURE;
    }

    // (2) Set the "reuse port" socket option
    const int enable{1};
    if (setsockopt(loadBalancer_fd, SOL_SOCKET, SO_REUSEADDR, &enable,
                   sizeof(enable)) == -1) {
      return EXIT_FAILURE;
    }

    // (3) Create a sockaddr_in struct for the proper port and bind() to it.
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));

    // (3.1): specify socket family.
    // This is an IPv4 socket.
    addr.sin_family = AF_INET;

    // (3.2): specify client address whitelist (hostname).
    // The socket will be a server listening to everybody.
    addr.sin_addr.s_addr = INADDR_ANY;

    // (3.3): Set the port value.
    // If port is 0, the OS will choose the port for us.
    // Use htons to convert from local byte order to network byte order.
    addr.sin_port = htons(static_cast<uint16_t>(loadBalancer_port));

    // (3.4) Bind to the port.
    if (bind(loadBalancer_fd, (sockaddr *)&addr, sizeof(addr)) == -1) {
      return EXIT_FAILURE;
    }

    // (4) Begin listening for clients.
    if (listen(loadBalancer_fd, MAX_NO_OF_PORTS) == -1) {
      return EXIT_FAILURE;
    }
    spdlog::info("Load balancer started on port {}", loadBalancer_port);

    int curr_server{0};
    while (true) {
      // (5) Accept clients.
      struct sockaddr_in client_addr;
      socklen_t client_addr_len{sizeof(client_addr)};
      int client_sockfd =
          accept(loadBalancer_fd, (sockaddr *)&client_addr, &client_addr_len);
      if (client_sockfd == -1) {
        return EXIT_FAILURE;
      }

      LoadBalancerRequest client_request;
      size_t no_of_bytes_read{};
      while (no_of_bytes_read < sizeof(client_request)) {
        long curr{recv(client_sockfd, &client_request + no_of_bytes_read,
                       sizeof(client_request) - no_of_bytes_read, 0)};
        if (curr <= 0) {
          return EXIT_FAILURE;
        }
        no_of_bytes_read += curr;
      }
      ip_addr.s_addr = client_request.client_addr;
      if (inet_ntop(AF_INET, &ip_addr, ip_str, sizeof(ip_str)) == NULL) {
        return EXIT_FAILURE;
      }
      spdlog::info("Received request for client {} with request ID {}", ip_str,
                   ntohs(client_request.request_id));

      LoadBalancerResponse loadBalancer_response;
      loadBalancer_response.videoserver_addr = server_ips[curr_server];
      loadBalancer_response.videoserver_port = htons(server_ports[curr_server]);
      loadBalancer_response.request_id = client_request.request_id;
      size_t no_of_bytes_sent{};
      while (no_of_bytes_sent < sizeof(loadBalancer_response)) {
        long curr{send(client_sockfd, &loadBalancer_response + no_of_bytes_sent,
                       sizeof(loadBalancer_response) - no_of_bytes_sent, 0)};
        if (curr <= 0) {
          return EXIT_FAILURE;
        }
        no_of_bytes_sent += curr;
      }
      ip_addr.s_addr = loadBalancer_response.videoserver_addr;
      if (inet_ntop(AF_INET, &ip_addr, ip_str, sizeof(ip_str)) == NULL) {
        return EXIT_FAILURE;
      }
      spdlog::info("Responded to request ID {} with server {}:{}",
                   ntohs(loadBalancer_response.request_id), ip_str,
                   server_ports[curr_server]);

      if (close(client_sockfd) == -1) {
        return EXIT_FAILURE;
      }
      curr_server = (curr_server + 1) % num_servers;
    }
  } else {
    FILE *server_info_file;
    if ((server_info_file = fopen(server_info_path.c_str(), "r")) == NULL) {
      return EXIT_FAILURE;
    }

    int num_nodes, num_clients{}, id_of_first_server;
    std::vector<std::string> client_ips{}, server_ips{};
    char identity_str[7], ip_str[16];
    bool is_first_server{true};
    if (fscanf(server_info_file, "%*s%d", &num_nodes) != 1) {
      return EXIT_FAILURE;
    }
    for (int i = 0; i < num_nodes; ++i) {
      if (fscanf(server_info_file, "%s%s", identity_str, ip_str) != 2) {
        return EXIT_FAILURE;
      }
      if (strcmp(identity_str, "CLIENT") == 0) {
        ++num_clients;
        client_ips.push_back(ip_str);
      } else if (strcmp(identity_str, "SERVER") == 0) {
        if (is_first_server) {
          id_of_first_server = i;
          is_first_server = false;
        }
        server_ips.push_back(ip_str);
      }
    }

    std::vector<std::vector<std::pair<int, int>>> adj_list{};
    int num_links, from, to, cost;
    for (int i = 0; i < num_nodes; i++) {
      adj_list.push_back({});
    }
    if (fscanf(server_info_file, "%*s%d", &num_links) != 1) {
      return EXIT_FAILURE;
    }
    for (int i = 0; i < num_links; ++i) {
      if (fscanf(server_info_file, "%d%d%d", &from, &to, &cost) != 3) {
        return EXIT_FAILURE;
      }
      adj_list[from].push_back({to, cost});
    }

    if (fclose(server_info_file) == EOF) {
      return EXIT_FAILURE;
    }

    std::unordered_map<std::string, std::string> closest_server_of{};
    for (int i = 0; i < num_clients; ++i) {
      std::vector<int> curr{dijkstra(adj_list, i, num_nodes)};
      int min{id_of_first_server};
      for (int j = id_of_first_server; j < num_nodes; j++) {
        if (curr[j] < curr[min]) {
          min = j;
        }
      }
      if (curr[min] == INT_MAX) {
        continue;
      }
      closest_server_of[client_ips[i]] = server_ips[min - id_of_first_server];
    }

    // (1) Create socket
    int loadBalancer_fd;
    if ((loadBalancer_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
      return EXIT_FAILURE;
    }

    // (2) Set the "reuse port" socket option
    const int enable{1};
    if (setsockopt(loadBalancer_fd, SOL_SOCKET, SO_REUSEADDR, &enable,
                   sizeof(enable)) == -1) {
      return EXIT_FAILURE;
    }

    // (3) Create a sockaddr_in struct for the proper port and bind() to it.
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));

    // (3.1): specify socket family.
    // This is an IPv4 socket.
    addr.sin_family = AF_INET;

    // (3.2): specify client address whitelist (hostname).
    // The socket will be a server listening to everybody.
    addr.sin_addr.s_addr = INADDR_ANY;

    // (3.3): Set the port value.
    // If port is 0, the OS will choose the port for us.
    // Use htons to convert from local byte order to network byte order.
    addr.sin_port = htons(static_cast<uint16_t>(loadBalancer_port));

    // (3.4) Bind to the port.
    if (bind(loadBalancer_fd, (sockaddr *)&addr, sizeof(addr)) == -1) {
      return EXIT_FAILURE;
    }

    // (4) Begin listening for clients.
    if (listen(loadBalancer_fd, MAX_NO_OF_PORTS) == -1) {
      return EXIT_FAILURE;
    }
    spdlog::info("Load balancer started on port {}", loadBalancer_port);

    while (true) {
      // (5) Accept clients.
      struct sockaddr_in client_addr;
      socklen_t client_addr_len{sizeof(client_addr)};
      int client_sockfd =
          accept(loadBalancer_fd, (sockaddr *)&client_addr, &client_addr_len);
      if (client_sockfd == -1) {
        return EXIT_FAILURE;
      }

      LoadBalancerRequest client_request;
      size_t no_of_bytes_read{};
      while (no_of_bytes_read < sizeof(client_request)) {
        long curr{recv(client_sockfd, &client_request + no_of_bytes_read,
                       sizeof(client_request) - no_of_bytes_read, 0)};
        if (curr <= 0) {
          return EXIT_FAILURE;
        }
        no_of_bytes_read += curr;
      }
      char ip_str[16];
      in_addr ip_addr;
      ip_addr.s_addr = client_request.client_addr;
      if (inet_ntop(AF_INET, &ip_addr, ip_str, sizeof(ip_str)) == NULL) {
        return EXIT_FAILURE;
      }
      spdlog::info("Received request for client {} with request ID {}", ip_str,
                   ntohs(client_request.request_id));

      LoadBalancerResponse loadBalancer_response;
      if (!closest_server_of.contains(ip_str)) {
        spdlog::info("Failed to fulfill request ID {}",
                     ntohs(client_request.request_id));
        if (close(client_sockfd) == -1) {
          return EXIT_FAILURE;
        }
        continue;
      } else {
        std::string closest_server{closest_server_of[ip_str]};
        if (inet_pton(AF_INET, closest_server.c_str(), &ip_addr) != 1) {
          return EXIT_FAILURE;
        }
        loadBalancer_response.videoserver_addr = ip_addr.s_addr;
      }
      loadBalancer_response.videoserver_port = htons(8000);
      loadBalancer_response.request_id = client_request.request_id;
      size_t no_of_bytes_sent{};
      while (no_of_bytes_sent < sizeof(loadBalancer_response)) {
        long curr{send(client_sockfd, &loadBalancer_response + no_of_bytes_sent,
                       sizeof(loadBalancer_response) - no_of_bytes_sent, 0)};
        if (curr <= 0) {
          return EXIT_FAILURE;
        }
        no_of_bytes_sent += curr;
      }
      ip_addr.s_addr = loadBalancer_response.videoserver_addr;
      if (inet_ntop(AF_INET, &ip_addr, ip_str, sizeof(ip_str)) == NULL) {
        return EXIT_FAILURE;
      }
      spdlog::info("Responded to request ID {} with server {}:{}",
                   ntohs(loadBalancer_response.request_id), ip_str, 8000);

      if (close(client_sockfd) == -1) {
        return EXIT_FAILURE;
      }
    }
  }
}
