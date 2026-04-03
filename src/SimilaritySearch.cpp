#include "SimilaritySearch.hpp"
#include "ImageHasher.hpp"
#include <algorithm>
#include <set>
#include <numeric>
#include <QtConcurrent>
#include <mutex>
#include <map>
 
// 高速なマージのための Union-Find (Disjoint Set Union)
struct DSU {
    std::vector<int> parent;
    DSU(int n) {
        parent.resize(n);
        std::iota(parent.begin(), parent.end(), 0);
    }
    int find(int i) {
        if (parent[i] == i) return i;
        return parent[i] = find(parent[i]);
    }
    void unite(int i, int j) {
        int root_i = find(i);
        int root_j = find(j);
        if (root_i != root_j) parent[root_i] = root_j;
    }
};
 
std::vector<DuplicateGroup> SimilaritySearch::findDuplicates(const std::vector<ImageData>& images, int threshold, bool strict) {
    if (images.empty()) return {};
 
    int n = static_cast<int>(images.size());
    DSU dsu(n);
    std::mutex dsuMutex;
 
    // インデックスのリストを作成 (C++20 std::views::iota の代わり)
    std::vector<int> indices(n);
    std::iota(indices.begin(), indices.end(), 0);
 
    // 外側ループを並列化
    // 各コアでハミング距離の計算を分散実行
    QtConcurrent::blockingMap(indices, [&](int i) {
        for (int j = i + 1; j < n; ++j) {

            int distD = ImageHasher::hammingDistance(images[i].dhash, images[j].dhash);
            int distP = ImageHasher::hammingDistance(images[i].phash, images[j].phash);
 
            bool similar = strict ? (distD <= threshold && distP <= threshold) 
                                  : (distD <= threshold || distP <= threshold);

            if (similar) {
                std::lock_guard<std::mutex> lock(dsuMutex);
                dsu.unite(i, j);
            }
        }
    });
 
    // 結果の集計 (ルートごとに画像をまとめる)
    std::map<int, DuplicateGroup> groupsMap;
    for (int i = 0; i < n; ++i) {
        int root = dsu.find(i);
        groupsMap[root].images.push_back(images[i]);
    }
 
    std::vector<DuplicateGroup> results;
    for (auto& pair : groupsMap) {
        if (pair.second.images.size() > 1) {
            results.push_back(pair.second);
        }
    }
 
    return results;
}
