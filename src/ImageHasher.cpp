#include "ImageHasher.hpp"
#include <bit> // C++20 std::popcount
#include <opencv2/imgproc.hpp>

// 画像の輝度勾配（隣り合うピクセルの明暗）からハッシュを計算する(Difference
// Hash) 処理が軽く、単純なリサイズ等に強い特徴がある
uint64_t ImageHasher::calculateDHash(const cv::Mat &image) {
  if (image.empty())
    return 0;

  cv::Mat gray, resized;
  cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
  // dHash needs 9x8 for 8x8 diffs
  cv::resize(gray, resized, cv::Size(9, 8));

  uint64_t hash = 0;
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      if (resized.at<uint8_t>(y, x) < resized.at<uint8_t>(y, x + 1)) {
        hash |= (1ULL << (y * 8 + x));
      }
    }
  }
  return hash;
}

// 画像の低周波成分（全体的な形状やぼんやりとした特徴）からハッシュを計算する(Perceptual
// Hash) 多少の色調変化やノイズに対してよりロバスト（堅牢）な特徴がある
uint64_t ImageHasher::calculatePHash(const cv::Mat &image) {
  if (image.empty())
    return 0;

  cv::Mat gray, resized, floatImg, dctImg;
  cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
  // Resize to 32x32 for DCT
  cv::resize(gray, resized, cv::Size(PHASH_SIZE, PHASH_SIZE));
  resized.convertTo(floatImg, CV_32F);

  cv::dct(floatImg, dctImg);

  // Take top-left 8x8 (excluding DC component at 0,0)
  double sum = 0;
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      if (x == 0 && y == 0)
        continue;
      sum += dctImg.at<float>(y, x);
    }
  }
  double avg = sum / 63.0;

  uint64_t hash = 0;
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      if (x == 0 && y == 0)
        continue;
      if (dctImg.at<float>(y, x) > avg) {
        hash |= (1ULL << (y * 8 + x));
      }
    }
  }
  return hash;
}

// ハミング距離を計算する (std::popcount でハードウェア命令を使い高速化)
/*
int ImageHasher::hammingDistance(uint64_t h1, uint64_t h2) {
    return static_cast<int>(std::popcount(h1 ^ h2));
}
*/

cv::Mat ImageHasher::loadImage(const std::string &path, int targetSize) {
  // Load with IMREAD_COLOR
  cv::Mat img = cv::imread(path, cv::IMREAD_COLOR);
  if (img.empty())
    return cv::Mat();

  // Minor optimization: if image is huge, resize it slightly for faster hashing
  if (img.cols > targetSize || img.rows > targetSize) {
    double scale =
        static_cast<double>(targetSize) / std::max(img.cols, img.rows);
    cv::resize(img, img, cv::Size(), scale, scale);
  }
  return img;
}
