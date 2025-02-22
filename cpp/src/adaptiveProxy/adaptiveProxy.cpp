#include "Random.h"
#include "http.h"
#include "loadBalancer_protocol.h"
#include "network_utils.h"
#include "spdlog/spdlog.h"
#include <cstdlib>
#include <cxxopts.hpp>
#include <sys/epoll.h>

int main(int argc, char *argv[]) {
  increase_fd_limit();

  cxxopts::Options cxxopts_options{
      "adaptiveProxy", "An HTTP Proxy for Adaptive Bitrate Selection"};
  cxxopts_options.add_options()(
      "b,balance",
      "The presence of this flag indicates that load balancing should occur.",
      cxxopts::value<bool>()->default_value("false"))(

      "l,listen-port",
      "The TCP port your proxy should listen on for accepting "
      "connections from your browser.",
      cxxopts::value<int>())(

      "h,hostname",
      "Argument specifying the IP address of the load balancer/video server.",
      cxxopts::value<std::string>())(
      "p,port",
      "Argument specifying the port of the load balancer/video "
      "server at the IP address described by hostname.",
      cxxopts::value<int>())(
      "a,alpha",
      "A float in the range [0, 1]. Used as the coefficient in EWMA "
      "throughput estimate.",
      cxxopts::value<double>());

  int adaptiveProxy_listen_port, videoserver_port;
  std::string videoserver_hostname;
  double alpha;
  bool is_balance;
  try {
    const auto cxxopts_argv{cxxopts_options.parse(argc, argv)};
    adaptiveProxy_listen_port = cxxopts_argv["listen-port"].as<int>();
    videoserver_hostname = cxxopts_argv["hostname"].as<std::string>();
    videoserver_port = cxxopts_argv["port"].as<int>();
    alpha = cxxopts_argv["alpha"].as<double>();
    is_balance = cxxopts_argv["balance"].as<bool>();
  } catch (const cxxopts::exceptions::parsing &e) {
    std::cout << e.what() << '\n';
    return EXIT_FAILURE;
  }
  if (1024 > adaptiveProxy_listen_port || adaptiveProxy_listen_port > 65535) {
    std::cout << "Error: listen_port must be in the range of [1024, 65535]\n";
    return EXIT_FAILURE;
  } else if (1024 > videoserver_port || videoserver_port > 65535) {
    std::cout << "Error: port must be in the range of [1024, 65535]\n";
    return EXIT_FAILURE;
  } else if (0 > alpha || alpha > 1) {
    std::cout << "Error: alpha must be in the range of [0, 1]\n";
    return EXIT_FAILURE;
  }

  std::unordered_map<int, int> videoserver_socket_for_client{},
      client_socket_for_videoserver;
  int loadBalancer_socket;

  int adaptiveProxy_socket{get_inbound_socket(adaptiveProxy_listen_port)};
  spdlog::info("adaptiveProxy started");

  int epoll_fd{epoll_create1(EPOLL_CLOEXEC)};
  if (epoll_fd == -1) {
    spdlog::warn("epoll_create()");
    return EXIT_FAILURE;
  }
  epoll_event event, events[MAX_NO_OF_PORTS];
  event.events = EPOLLIN;
  event.data.fd = adaptiveProxy_socket;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, adaptiveProxy_socket, &event) == -1) {
    spdlog::warn("epoll_ctl_add()");
    return EXIT_FAILURE;
  }

  std::unordered_map<std::string, unsigned long> throughput_of_client{};
  std::unordered_map<std::string, std::vector<int>> bitrate_of_video{};

  char ip_str[16];
  in_addr ip_addr;
  char buffer[MAX_VIDEO_SIZE];
  while (true) {
    int no_of_events{epoll_wait(epoll_fd, events, MAX_NO_OF_PORTS, -1)};
    if (no_of_events == -1) {
      spdlog::warn("epoll_wait()");
      return EXIT_FAILURE;
    }
    for (int i{0}; i < no_of_events; ++i) {
      if (events[i].data.fd == adaptiveProxy_socket) {
        sockaddr_in client_addr;
        socklen_t client_addr_len{sizeof(client_addr)};
        int client_socket{accept(adaptiveProxy_socket, (sockaddr *)&client_addr,
                                 &client_addr_len)};
        if (client_socket == -1) {
          spdlog::warn("accept()");
          return EXIT_FAILURE;
        }

        ip_addr.s_addr = client_addr.sin_addr.s_addr;
        if (inet_ntop(AF_INET, &ip_addr, ip_str, sizeof(ip_str)) == NULL) {
          spdlog::warn("inet_ntop()");
          return EXIT_FAILURE;
        }
        spdlog::info("New client socket connected with {}:{} on sockfd {}",
                     ip_str, ntohs(client_addr.sin_port), client_socket);

        event.data.fd = client_socket;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &event) == -1) {
          spdlog::warn("epoll_ctl_add()");
          return EXIT_FAILURE;
        }

        if (is_balance) {
          loadBalancer_socket = get_outbound_socket(
              videoserver_hostname.c_str(), videoserver_port);
          LoadBalancerRequest loadBalancer_request;
          loadBalancer_request.client_addr = client_addr.sin_addr.s_addr;
          loadBalancer_request.request_id = htons(Random::get(0, UINT16_MAX));
          size_t no_of_bytes_sent{};
          while (no_of_bytes_sent < sizeof(loadBalancer_request)) {
            long curr{send(loadBalancer_socket,
                           &loadBalancer_request + no_of_bytes_sent,
                           sizeof(loadBalancer_request) - no_of_bytes_sent, 0)};
            if (curr == -1) {
              spdlog::warn("loadBalancer_request send()");
              return EXIT_FAILURE;
            }
            no_of_bytes_sent += curr;
          }

          LoadBalancerResponse loadBalancer_response;
          size_t no_of_bytes_read{};
          while (no_of_bytes_read < sizeof(loadBalancer_response)) {
            long curr{recv(
                loadBalancer_socket, &loadBalancer_response + no_of_bytes_read,
                sizeof(loadBalancer_response) - no_of_bytes_read, 0)};
            if (curr == -1) {
              spdlog::warn("loadBalancer_request recv()");
              return EXIT_FAILURE;
            }
            no_of_bytes_read += curr;
          }
          if (loadBalancer_response.request_id !=
              loadBalancer_request.request_id) {
            spdlog::warn("loadBalancer request_id");
            return EXIT_FAILURE;
          }
          ip_addr.s_addr = loadBalancer_response.videoserver_addr;
          if (inet_ntop(AF_INET, &ip_addr, ip_str, sizeof(ip_str)) == NULL) {
            spdlog::warn("inet_ntop()");
            return EXIT_FAILURE;
          }
          videoserver_socket_for_client[client_socket] = get_outbound_socket(
              ip_str, ntohs(loadBalancer_response.videoserver_port));
          if (close(loadBalancer_socket) == -1) {
            spdlog::warn("close()");
            return EXIT_FAILURE;
          }
        } else {
          videoserver_socket_for_client[client_socket] = get_outbound_socket(
              videoserver_hostname.c_str(), videoserver_port);
        }
        client_socket_for_videoserver
            [videoserver_socket_for_client[client_socket]] = client_socket;
        event.data.fd = videoserver_socket_for_client[client_socket];
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD,
                      videoserver_socket_for_client[client_socket],
                      &event) == -1) {
          spdlog::warn("epoll_ctl_add()");
          return EXIT_FAILURE;
        }
      } else if (videoserver_socket_for_client.contains(events[i].data.fd)) {
        int client_socket{events[i].data.fd};
        size_t msg_len;
        try {
          msg_len = recv_one_http(client_socket, buffer);
        } catch (const std::runtime_error &e) {
          spdlog::info("Client socket sockfd {} disconnected", client_socket);
          if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL,
                        videoserver_socket_for_client[client_socket],
                        NULL) == -1) {
            spdlog::warn("epoll_ctl_del()");
            return EXIT_FAILURE;
          }
          if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket, NULL) == -1) {
            spdlog::warn("epoll_ctl_del()");
            return EXIT_FAILURE;
          }
          if (close(videoserver_socket_for_client[client_socket]) == -1) {
            spdlog::warn("close()");
            return EXIT_FAILURE;
          }
          if (close(client_socket) == -1) {
            spdlog::warn("close()");
            return EXIT_FAILURE;
          }
          client_socket_for_videoserver.erase(
              videoserver_socket_for_client[client_socket]);
          videoserver_socket_for_client.erase(client_socket);
          continue;
        }

        if (is_post_on_fragment_received(buffer)) {
          std::string uuid;
          unsigned long fragment_size, start, end;
          parse_post_on_fragment_received(buffer, uuid, fragment_size, start,
                                          end);

          if (!throughput_of_client.contains(uuid)) {
            throughput_of_client[uuid] = 0;
          }
          throughput_of_client[uuid] =
              alpha *
                  ((fragment_size / 1000.0 * 8.0) / ((end - start) / 1000.0)) +
              (1.0 - alpha) * throughput_of_client[uuid];

          spdlog::info(
              "Client {} finished receiving a segment of size {} bytes in {} "
              "ms. Throughput: {} Kbps. Avg Throughput: {} Kbps",
              uuid, fragment_size, end - start,
              (unsigned long)((fragment_size / 1000.0 * 8.0) /
                              ((end - start) / 1000.0)),
              throughput_of_client[uuid]);
          try {
            send_one_http(client_socket, OK, sizeof(OK) - 1);
          } catch (const std::runtime_error &e) {
            spdlog::info("Client socket sockfd {} disconnected", client_socket);
            if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL,
                          videoserver_socket_for_client[client_socket],
                          NULL) == -1) {
              spdlog::warn("epoll_ctl_del()");
              return EXIT_FAILURE;
            }
            if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket, NULL) == -1) {
              spdlog::warn("epoll_ctl_del()");
              return EXIT_FAILURE;
            }
            if (close(videoserver_socket_for_client[client_socket]) == -1) {
              spdlog::warn("close()");
              return EXIT_FAILURE;
            }
            if (close(client_socket) == -1) {
              spdlog::warn("close()");
              return EXIT_FAILURE;
            }
            client_socket_for_videoserver.erase(
                videoserver_socket_for_client[client_socket]);
            videoserver_socket_for_client.erase(client_socket);
          }
        } else if (is_get_vid_mpd(buffer)) {
          sockaddr_in socket_addr;
          socklen_t socket_addr_len{sizeof(socket_addr)};
          if (getpeername(videoserver_socket_for_client[client_socket],
                          (sockaddr *)&socket_addr, &socket_addr_len) == -1) {
            spdlog::warn("getpeername()");
            return EXIT_FAILURE;
          }
          ip_addr.s_addr = socket_addr.sin_addr.s_addr;
          if (inet_ntop(AF_INET, &ip_addr, ip_str, sizeof(ip_str)) == NULL) {
            spdlog::warn("inet_ntop()");
            return EXIT_FAILURE;
          }

          std::string path_to_video, no_list_mpd, uuid;
          parse_get_vid_mpd(buffer, path_to_video, uuid);

          if (!bitrate_of_video.contains(path_to_video)) {
            get_bitrate_of_video(videoserver_socket_for_client[client_socket],
                                 buffer, bitrate_of_video, path_to_video);
          }
          no_list_mpd =
              "GET " + path_to_video +
              "/vid-no-list.mpd HTTP/1.1\r\ncontent-length: 0\r\n\r\n";
          send_one_http(videoserver_socket_for_client[client_socket],
                        no_list_mpd.c_str(), no_list_mpd.length());

          spdlog::info("Manifest requested by {} forwarded to {}:{} for {}",
                       uuid, ip_str, ntohs(socket_addr.sin_port),
                       path_to_video + "/vid-no-list.mpd");
        } else if (is_get_vid_m4s(buffer)) {
          std::string path_to_video, m4s, uuid, segment_no;
          parse_get_vid_m4s(buffer, path_to_video, uuid, segment_no);

          int bitrate{bitrate_of_video[path_to_video][0]};
          unsigned long curr_throughput{throughput_of_client[uuid]};
          for (size_t i{bitrate_of_video[path_to_video].size()}; i-- > 0;) {
            if ((curr_throughput / 1.5) >= bitrate_of_video[path_to_video][i]) {
              bitrate = bitrate_of_video[path_to_video][i];
              break;
            }
          }

          m4s = "GET " + path_to_video + "/video/vid-" +
                std::to_string(bitrate) + "-seg-" + segment_no +
                ".m4s HTTP/1.1\r\ncontent-length: 0\r\n\r\n";
          send_one_http(videoserver_socket_for_client[client_socket],
                        m4s.c_str(), m4s.length());

          sockaddr_in socket_addr;
          socklen_t socket_addr_len{sizeof(socket_addr)};
          if (getpeername(videoserver_socket_for_client[client_socket],
                          (sockaddr *)&socket_addr, &socket_addr_len) == -1) {
            spdlog::warn("getpeername()");
            return EXIT_FAILURE;
          }
          ip_addr.s_addr = socket_addr.sin_addr.s_addr;
          if (inet_ntop(AF_INET, &ip_addr, ip_str, sizeof(ip_str)) == NULL) {
            spdlog::warn("inet_ntop()");
            return EXIT_FAILURE;
          }
          spdlog::info("Segment requested by {} forwarded to {}:{} as {} at "
                       "bitrate {} Kbps",
                       uuid, ip_str, ntohs(socket_addr.sin_port),
                       path_to_video + "/video/vid-" + std::to_string(bitrate) +
                           "-seg-" + segment_no + ".m4s",
                       bitrate);
        } else {
          send_one_http(videoserver_socket_for_client[client_socket], buffer,
                        msg_len);
        }
      } else {
        int videoserver_socket{events[i].data.fd};
        size_t msg_len{recv_one_http(videoserver_socket, buffer)};
        try {
          send_one_http(client_socket_for_videoserver[videoserver_socket],
                        buffer, msg_len);
        } catch (const std::runtime_error &e) {
          spdlog::info("Client socket sockfd {} disconnected",
                       client_socket_for_videoserver[videoserver_socket]);
          if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, videoserver_socket, NULL) ==
              -1) {
            spdlog::warn("epoll_ctl_del()");
            return EXIT_FAILURE;
          }
          if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL,
                        videoserver_socket_for_client[videoserver_socket],
                        NULL) == -1) {
            spdlog::warn("epoll_ctl_del()");
            return EXIT_FAILURE;
          }
          if (close(client_socket_for_videoserver[videoserver_socket]) == -1) {
            spdlog::warn("close()");
            return EXIT_FAILURE;
          }
          if (close(videoserver_socket) == -1) {
            spdlog::warn("close()");
            return EXIT_FAILURE;
          }
          client_socket_for_videoserver.erase(
              client_socket_for_videoserver[videoserver_socket]);
          videoserver_socket_for_client.erase(videoserver_socket);
        }
      }
    }
  }
}
