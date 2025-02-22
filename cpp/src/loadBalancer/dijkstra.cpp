#include "djikstra.h"

std::vector<int>
dijkstra(const std::vector<std::vector<std::pair<int, int>>> &adj, int start,
         int n) {
  std::vector<int> dist(n, INT_MAX);
  dist[start] = 0;
  std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>,
                      std::greater<std::pair<int, int>>>
      pq;
  pq.push({0, start});

  while (!pq.empty()) {
    int u = pq.top().second;
    int currentDist = pq.top().first;
    pq.pop();

    // Skip if a shorter path to u has already been found
    if (currentDist > dist[u])
      continue;

    // Explore each neighbor of u
    for (const auto &edge : adj[u]) {
      int v = edge.first;
      int weight = edge.second;

      // Relax the edge if a shorter path is found
      if (dist[v] > dist[u] + weight) {
        dist[v] = dist[u] + weight;
        pq.push({dist[v], v});
      }
    }
  }

  return dist;
}
