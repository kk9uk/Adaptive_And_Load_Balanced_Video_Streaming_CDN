#pragma once

#include <cstdint>
#include <netinet/in.h>

// Define the protocol between the load balancer and the adaptive proxy.

// The adaptive proxy sends a request to the load balancer containing the IP
// address of the client it wishes to serve.

// The load balancer replies with the (address, port) of the videoserver that
// the adaptive proxy should use for that particular client.

struct LoadBalancerRequest {
  in_addr_t client_addr; // The IP address of the client.
  uint16_t request_id;   // A randomly-chosen identifier for this request.
};

struct LoadBalancerResponse {
  in_addr_t videoserver_addr; // The IP address of the videoserver.
  uint16_t videoserver_port;  // The port of the videoserver.
  uint16_t request_id;        // The request_id from the LoadBalancerRequest.
};
