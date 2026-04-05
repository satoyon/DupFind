#pragma once
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <cstdint>

class ImageHasher {
public:
    // dHash（隣接ピクセルの輝度差分によるハッシュ）を計算する
    // 戻り値: 64ビット整数値のハッシュ
    static uint64_t calculateDHash(const cv::Mat& image);

    // pHash（DCT: 離散コサイン変換を用いた周波数特徴ハッシュ）を計算する
    // 戻り値: 64ビット整数値のハッシュ
    static uint64_t calculatePHash(const cv::Mat& image);

    // 2つのハッシュ値間のハミング距離（異なるビットの数）を計算する
    static int hammingDistance(uint64_t h1, uint64_t h2);

    // 大容量画像を高速に読み込むためのヘルパー関数
    // targetSize: 縮小後の目安となる最大サイズ
    static cv::Mat loadImage(const std::string& path, int targetSize = 512);

private:
    static constexpr int DHASH_SIZE = 8;
    static constexpr int PHASH_SIZE = 32;
};
