#ifndef HTTP_H
#define HTTP_H

#include "pugixml.hpp"
#include "spdlog/spdlog.h"
#include <boost/regex.hpp>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <sys/socket.h>

#define MAX_VIDEO_SIZE (2 * 1000 * 1000)

#define OK "HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n"

size_t recv_one_http(int socket, char *buffer);

void send_one_http(int socket, const char *msg, size_t msg_len);

bool is_post_on_fragment_received(const char *msg);

void parse_post_on_fragment_received(const char *msg, std::string &uuid,
                                     unsigned long &fragment_size,
                                     unsigned long &start, unsigned long &end);

bool is_get_vid_mpd(const char *msg);

void parse_get_vid_mpd(const char *msg, std::string &path_to_video,
                       std::string &uuid);

void get_bitrate_of_video(
    int socket, char *buffer,
    std::unordered_map<std::string, std::vector<int>> &bitrate_of_video,
    const std::string &path_to_video);

bool is_get_vid_m4s(const char *msg);

void parse_get_vid_m4s(const char *msg, std::string &path_to_video,
                       std::string &uuid, std::string &segment_no);

#endif // !HTTP_H
