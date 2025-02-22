#include "http.h"

bool is_header_end(char *buffer, size_t no_of_bytes_read) {
  char *start{buffer + no_of_bytes_read - 4};
  return start[0] == '\r' && start[1] == '\n' && start[2] == '\r' &&
         start[3] == '\n';
}

int get_content_length(const char *header) {
  try {
    boost::regex content_length_regex{"content-length:\\s*(\\d+)\\r\\n",
                                      boost::regex_constants::icase};
    boost::cmatch capture_groups;
    if (boost::regex_search(header, capture_groups, content_length_regex)) {
      return std::stoi(capture_groups[1].str());
    } else {
      return 0;
    }
  } catch (const std::exception &e) {
    std::cout << e.what() << '\n';
    quick_exit(EXIT_FAILURE);
  }
}

size_t recv_one_http(int socket, char *buffer) {
  size_t no_of_bytes_of_header_read{};

  while (no_of_bytes_of_header_read < 4 ||
         !is_header_end(buffer, no_of_bytes_of_header_read)) {
    long curr{recv(socket, buffer + no_of_bytes_of_header_read, 1, 0)};
    if (curr == 0) {
      spdlog::warn("recv_one_http(): header, socket {} disconnected", socket);
      throw std::runtime_error("");
    } else if (curr < 0) {
      spdlog::warn("recv_one_http(): header");
      quick_exit(EXIT_FAILURE);
    }
    ++no_of_bytes_of_header_read;
  }
  buffer[no_of_bytes_of_header_read] = '\0';

  int content_length{get_content_length(buffer)};
  size_t no_of_bytes_of_body_read{};
  while (no_of_bytes_of_body_read < content_length) {
    long curr{recv(
        socket, buffer + no_of_bytes_of_header_read + no_of_bytes_of_body_read,
        content_length - no_of_bytes_of_body_read, 0)};
    if (curr == 0) {
      spdlog::warn("recv_one_http(): body, socket {} disconnected", socket);
      throw std::runtime_error("");
    } else if (curr < 0) {
      spdlog::warn("recv_one_http(): body");
      quick_exit(EXIT_FAILURE);
    }
    no_of_bytes_of_body_read += curr;
  }
  buffer[no_of_bytes_of_header_read + no_of_bytes_of_body_read] = '\0';

  return no_of_bytes_of_header_read + no_of_bytes_of_body_read;
}

void send_one_http(int socket, const char *msg, size_t msg_len) {
  size_t no_of_bytes_sent{};
  while (no_of_bytes_sent < msg_len) {
    long curr{
        send(socket, msg + no_of_bytes_sent, msg_len - no_of_bytes_sent, 0)};
    if (curr == 0) {
      spdlog::warn("send_one_http(): socket {} disconnected", socket);
      throw std::runtime_error("");
    } else if (curr < 0) {
      spdlog::warn("send_one_http()");
      quick_exit(EXIT_FAILURE);
    }
    no_of_bytes_sent += curr;
  }
}

bool is_post_on_fragment_received(const char *msg) {
  try {
    boost::regex post_on_fragment_received_regex{
        "POST\\s*/on-fragment-received", boost::regex_constants::icase};
    return boost::regex_search(msg, post_on_fragment_received_regex);
  } catch (const std::exception &e) {
    std::cout << e.what() << '\n';
    quick_exit(EXIT_FAILURE);
  }
}

void parse_post_on_fragment_received(const char *msg, std::string &uuid,
                                     unsigned long &fragment_size,
                                     unsigned long &start, unsigned long &end) {
  try {
    boost::cmatch capture_groups;

    boost::regex uuid_regex{"x-489-uuid:\\s([^\\r\\n]+)\\r\\n",
                            boost::regex_constants::icase};
    boost::regex_search(msg, capture_groups, uuid_regex);
    uuid = capture_groups[1].str();
    capture_groups = boost::cmatch{};

    boost::regex fragment_size_regex{"x-fragment-size:\\s*(\\d+)\\r\\n",
                                     boost::regex_constants::icase};
    boost::regex_search(msg, capture_groups, fragment_size_regex);
    fragment_size = std::stoul(capture_groups[1].str());
    capture_groups = boost::cmatch{};

    boost::regex start_regex{"x-timestamp-start:\\s*(\\d+)\\r\\n",
                             boost::regex_constants::icase};
    boost::regex_search(msg, capture_groups, start_regex);
    start = std::stoul(capture_groups[1].str());
    capture_groups = boost::cmatch{};

    boost::regex end_regex{"x-timestamp-end:\\s*(\\d+)\\r\\n",
                           boost::regex_constants::icase};
    boost::regex_search(msg, capture_groups, end_regex);
    end = std::stoul(capture_groups[1].str());
  } catch (const std::exception &e) {
    std::cout << e.what() << '\n';
    quick_exit(EXIT_FAILURE);
  }
}

bool is_get_vid_mpd(const char *msg) {
  try {
    boost::regex get_vid_mpd_regex{"GET\\s*.*/vid\\.mpd",
                                   boost::regex_constants::icase};
    return boost::regex_search(msg, get_vid_mpd_regex);
  } catch (const std::exception &e) {
    std::cout << e.what() << '\n';
    quick_exit(EXIT_FAILURE);
  }
}

void parse_get_vid_mpd(const char *msg, std::string &path_to_video,
                       std::string &uuid) {
  try {
    boost::cmatch capture_groups;

    boost::regex path_to_video_regex{"GET\\s*(.*)/vid\\.mpd",
                                     boost::regex_constants::icase};
    boost::regex_search(msg, capture_groups, path_to_video_regex);
    path_to_video = capture_groups[1].str();
    capture_groups = boost::cmatch{};

    boost::regex uuid_regex{"x-489-uuid:\\s([^\\r\\n]+)\\r\\n",
                            boost::regex_constants::icase};
    boost::regex_search(msg, capture_groups, uuid_regex);
    uuid = capture_groups[1].str();
  } catch (const std::exception &e) {
    std::cout << e.what() << '\n';
    quick_exit(EXIT_FAILURE);
  }
}

void get_bitrate_of_video(
    int socket, char *buffer,
    std::unordered_map<std::string, std::vector<int>> &bitrate_of_video,
    const std::string &path_to_video) {
  const std::string mpd{"GET " + path_to_video +
                        "/vid.mpd HTTP/1.1\r\ncontent-length: 0\r\n\r\n"};
  size_t no_of_bytes_sent{};
  while (no_of_bytes_sent < mpd.length()) {
    long curr{send(socket, mpd.c_str() + no_of_bytes_sent,
                   mpd.length() - no_of_bytes_sent, 0)};
    if (curr == -1) {
      spdlog::warn("get_bitrate_of_video(): send()");
      quick_exit(EXIT_FAILURE);
    }
    no_of_bytes_sent += curr;
  }

  size_t no_of_bytes_of_header_read{};
  while (no_of_bytes_of_header_read < 4 ||
         !is_header_end(buffer, no_of_bytes_of_header_read)) {
    long curr{recv(socket, buffer + no_of_bytes_of_header_read, 1, 0)};
    if (curr == -1) {
      spdlog::warn("get_bitrate_of_video(): recv() header");
      quick_exit(EXIT_FAILURE);
    }
    ++no_of_bytes_of_header_read;
  }
  buffer[no_of_bytes_of_header_read] = '\0';

  int content_length{get_content_length(buffer)};
  size_t no_of_bytes_of_body_read{};
  while (no_of_bytes_of_body_read < content_length) {
    long curr{recv(socket,
                   buffer + no_of_bytes_of_body_read, // overwrites header
                   content_length - no_of_bytes_of_body_read, 0)};
    if (curr == -1) {
      spdlog::warn("get_bitrate_of_video(): recv() body");
      quick_exit(EXIT_FAILURE);
    }
    no_of_bytes_of_body_read += curr;
  }
  buffer[no_of_bytes_of_body_read] = '\0';

  bitrate_of_video[path_to_video] = {};
  pugi::xml_document xml;
  pugi::xml_parse_result parsed_xml{
      xml.load_buffer_inplace(buffer, no_of_bytes_of_body_read + 1)};
  if (!parsed_xml) {
    spdlog::warn("!parsed_xml");
    quick_exit(EXIT_FAILURE);
  }
  for (pugi::xml_node adaptation_set :
       xml.child("MPD").child("Period").children("AdaptationSet")) {
    pugi::xml_attribute mime_type{adaptation_set.attribute("mimeType")};
    if (mime_type && std::string{mime_type.value()} == "video/mp4") {
      for (pugi::xml_node representation :
           adaptation_set.children("Representation")) {
        pugi::xml_attribute bandwidth{representation.attribute("bandwidth")};
        if (bandwidth) {
          bitrate_of_video[path_to_video].push_back(bandwidth.as_int());
        }
      }
    }
  }
}

bool is_get_vid_m4s(const char *msg) {
  try {
    boost::regex get_vid_m4s_regex{"GET\\s*.*/video/vid-\\d+-seg-\\d+.m4s",
                                   boost::regex_constants::icase};
    return boost::regex_search(msg, get_vid_m4s_regex);
  } catch (const std::exception &e) {
    std::cout << e.what() << '\n';
    quick_exit(EXIT_FAILURE);
  }
}

void parse_get_vid_m4s(const char *msg, std::string &path_to_video,
                       std::string &uuid, std::string &segment_no) {
  try {
    boost::cmatch capture_groups;

    boost::regex path_to_video_regex{"GET\\s*(.*)/video/vid-\\d+-seg-\\d+.m4s",
                                     boost::regex_constants::icase};
    boost::regex_search(msg, capture_groups, path_to_video_regex);
    path_to_video = capture_groups[1].str();
    capture_groups = boost::cmatch{};

    boost::regex uuid_regex{"x-489-uuid:\\s([^\\r\\n]+)\\r\\n",
                            boost::regex_constants::icase};
    boost::regex_search(msg, capture_groups, uuid_regex);
    uuid = capture_groups[1].str();
    capture_groups = boost::cmatch{};

    boost::regex segment_no_regex{"GET\\s*.*/video/vid-\\d+-seg-(\\d+).m4s",
                                  boost::regex_constants::icase};
    boost::regex_search(msg, capture_groups, segment_no_regex);
    segment_no = capture_groups[1].str();
  } catch (const std::exception &e) {
    std::cout << e.what() << '\n';
    quick_exit(EXIT_FAILURE);
  }
}
