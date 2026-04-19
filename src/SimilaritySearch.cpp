#include "SimilaritySearch.hpp"
#include "ImageHasher.hpp"
#include <QThread>
#include <QtConcurrent>
#include <algorithm>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
#include <utility>

// 高速なマージのための Union-Find (Disjoint Set Union)
struct DSU {
  std::vector<int> parent;
  DSU(int n) {
    parent.resize(n);
    std::iota(parent.begin(), parent.end(), 0);
  }
  int find(int i) {
    if (parent[i] == i)
      return i;
    return parent[i] = find(parent[i]);
  }
  void unite(int i, int j) {
    int root_i = find(i);
    int root_j = find(j);
    if (root_i != root_j)
      parent[root_i] = root_j;
  }
};

std::vector<DuplicateGroup>
SimilaritySearch::findDuplicates(const std::vector<ImageData> &images,
                                 int threshold, bool strict) {
  if (images.empty())
    return {};

  int n = static_cast<int>(images.size());
  DSU dsu(n);
  std::mutex dsuMutex;

  // スレッドプール数に合わせて処理をチャンク分割して実行
  int numThreads = QThread::idealThreadCount();
  if (numThreads <= 0)
    numThreads = 8;

  // タスク数（チャンク数）をスレッド数より多く設定し、スレッドプールによる動的負荷分散を効かせる
  int numChunks = numThreads * 16;

  struct Chunk {
    int start_i;
    int end_i;
  };
  std::vector<Chunk> chunks;

  // 全体の比較回数を計算し、各チャンクがおおむね均等な比較回数（処理量）になるように分割
  long long totalPairs = static_cast<long long>(n) * (n - 1) / 2;
  long long pairsPerChunk = std::max(1LL, totalPairs / numChunks);

  int current_start = 0;
  long long current_pairs = 0;
  for (int i = 0; i < n; ++i) {
    current_pairs += (n - 1 - i);
    // 各チャンクの処理量が目標値に達するか、最後の行に到達したらチャンクとして登録
    if (current_pairs >= pairsPerChunk || i == n - 1) {
      chunks.push_back({current_start, i + 1});
      current_start = i + 1;
      current_pairs = 0;
    }
  }

  auto mapper = [&](Chunk &chunk) {
    std::vector<std::pair<int, int>> local_edges;
    local_edges.reserve(1024); // 一時バッファ
    for (int i = chunk.start_i; i < chunk.end_i; ++i) {
      for (int j = i + 1; j < n; ++j) {
        int distD =
            ImageHasher::hammingDistance(images[i].dhash, images[j].dhash);
        int distP =
            ImageHasher::hammingDistance(images[i].phash, images[j].phash);
        bool similar = strict ? (distD <= threshold && distP <= threshold)
                              : (distD <= threshold || distP <= threshold);
        if (similar) {
          local_edges.emplace_back(i, j);
          
          // バッファが一定量溜まったら、排他制御でDSUにマージして解放
          // 全結果を最後に結合するのを避け、メモリ使用量とヒープの競合を極小化
          if (local_edges.size() >= 1024) {
            std::lock_guard<std::mutex> lock(dsuMutex);
            for (const auto &edge : local_edges) {
              dsu.unite(edge.first, edge.second);
            }
            local_edges.clear();
          }
        }
      }
    }
    // 残りのエッジをマージ
    if (!local_edges.empty()) {
      std::lock_guard<std::mutex> lock(dsuMutex);
      for (const auto &edge : local_edges) {
        dsu.unite(edge.first, edge.second);
      }
    }
  };

  // 結果のリストを構築せず、チャンクごとに並列処理しながらインプレースでマージを実行
  QtConcurrent::blockingMap(chunks, mapper);

  // 結果の集計 (ルートごとに画像をまとめる)
  std::map<int, DuplicateGroup> groupsMap;
  for (int i = 0; i < n; ++i) {
    int root = dsu.find(i);
    groupsMap[root].images.push_back(images[i]);
  }

  std::vector<DuplicateGroup> results;
  for (auto &pair : groupsMap) {
    if (pair.second.images.size() > 1) {
      results.push_back(pair.second);
    }
  }

  return results;
}


std::vector<ImageData> SimilaritySearch::findSimilarImages(
    const ImageData &image, const std::vector<ImageData> &images, int threshold,
    bool strict) {
  auto FilterFunc = [&](const ImageData &cand) {
    if (cand.path == image.path)
      return false;
    int distD = ImageHasher::hammingDistance(image.dhash, cand.dhash);
    int distP = ImageHasher::hammingDistance(image.phash, cand.phash);
    bool similar = strict ? (distD <= threshold && distP <= threshold)
                          : (distD <= threshold || distP <= threshold);
    return similar;
  };

  return QtConcurrent::blockingFiltered(images, FilterFunc);
}
