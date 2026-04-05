#pragma once
#include "DatabaseManager.hpp"
#include <vector>
#include <map>

// 重複画像と判定された画像のグループをまとめる構造体
struct DuplicateGroup {
    std::vector<ImageData> images;
};

// 類似画像（重複）検索を処理するクラス
class SimilaritySearch {
public:
    // 指定された画像リストから重複画像のグループを抽出する
    // threshold: ハミング距離の許容閾値（これ以下なら類似とみなす）
    // strict: trueならdHashとpHashの両方、falseなら片方が閾値以下であれば類似と判定
    static std::vector<DuplicateGroup> findDuplicates(const std::vector<ImageData>& images, int threshold = 5, bool strict = false);

private:
    // 内部で使用されるシンプルなクラスタリングアルゴリズム
};
