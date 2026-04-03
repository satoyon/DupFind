#pragma once
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <cstdint>

class ImageHasher {
public:
    // dHash: 64-bit integer
    static uint64_t calculateDHash(const cv::Mat& image);

    // pHash: 64-bit integer
    static uint64_t calculatePHash(const cv::Mat& image);

    // Hamming distance
    static int hammingDistance(uint64_t h1, uint64_t h2);

    // Helper for loading large images
    static cv::Mat loadImage(const std::string& path, int targetSize = 512);

private:
    static constexpr int DHASH_SIZE = 8;
    static constexpr int PHASH_SIZE = 32;
};
