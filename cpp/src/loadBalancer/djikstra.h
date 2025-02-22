#ifndef DIJKSTRA_H
#define DIJKSTRA_H

#include <climits>
#include <queue>
#include <vector>

std::vector<int>
dijkstra(const std::vector<std::vector<std::pair<int, int>>> &adj, int start,
         int n);

#endif // !DIJKSTRA_H
