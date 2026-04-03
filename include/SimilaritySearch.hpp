#pragma once
#include "DatabaseManager.hpp"
#include <vector>
#include <map>

struct DuplicateGroup {
    std::vector<ImageData> images;
};

class SimilaritySearch {
public:
    static std::vector<DuplicateGroup> findDuplicates(const std::vector<ImageData>& images, int threshold = 5, bool strict = false);

private:
    // Simple clustering algorithm
};
