# DupFind - 画像類似度検索ツール

現在の開発状況と継続方法について記述します。

## 現在のステータス
- **計画フェーズ**: 実施計画 (`implementation_plan.md`) とタスクリスト (`task.md`) の作成が完了し、ユーザーからのフィードバック（複数ディレクトリ対応、Linux対応）を反映済みです。
- **未着手**: 実装（CMake プロジェクトのセットアップ以降）はこれから開始する段階です。

## 継続方法
作業を再開する際は、このディレクトリにある `implementation_plan.md` と `task.md` を読み込み、以下のステップから開始してください：
1. CMake プロジェクトの作成と OpenCV/Qt 6/SQLite の依存関係のセットアップ。
2. コアロジック（ImageHasher）の実装。

## 依存ツール
- C++ コンパイラ (MSVC または GCC)
- CMake
- OpenCV
- Qt 6
- SQLite3
