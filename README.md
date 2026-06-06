# ZEROnOisE

Shanling M3 Ultra 専用 Bit-Perfect オーディオプレイヤー  
設計書 v1.1 準拠

---

## APK の取得方法（3通り）

### ① GitHub Actions でビルド（推奨・最も簡単）

Android Studio 不要。GitHub アカウントさえあれば OK。

```
1. GitHub で新規リポジトリを作成
2. このプロジェクトをプッシュ:
     git init
     git add .
     git commit -m "init"
     git remote add origin https://github.com/<あなた>/ZEROnOisE.git
     git push -u origin main

3. GitHub の Actions タブ → "Build ZEROnOisE APK" → Run workflow

4. 完了後: Actions → 最新ワークフロー → Artifacts
     → ZEROnOisE-debug-N.zip をダウンロード → APK を取り出す

5. M3 Ultra に転送してインストール
```

ビルド時間: 初回 約15分（libFLAC ビルド含む）、2回目以降 約5分（キャッシュ）

---

### ② Docker でビルド（ローカルマシン）

```bash
mkdir apk-output
docker build -t zeronoise-builder .
docker run --rm -v "$(pwd)/apk-output:/output" zeronoise-builder
# → apk-output/ZEROnOisE-debug.apk が生成される
```

---

### ③ Android Studio でビルド

```
File → Open → このフォルダを選択
SDK Manager → NDK r26+ と CMake 3.22.1 をインストール
Build → Build Bundle(s) / APK(s) → Build APK(s)
```

---

## M3 Ultra へのインストール

```
1. M3 Ultra の設定 → セキュリティ → 提供元不明のアプリ → 許可
2. APK を USB または microSD で転送
3. ファイルマネージャーから APK をタップしてインストール
```

---

## 音質設計の要点

| 項目 | 実装 |
|---|---|
| リサンプリング禁止 | setSampleRate() にソース SR をそのまま指定 |
| ミキサーバイパス | AAUDIO_SHARING_MODE_EXCLUSIVE |
| 24bit 無劣化 | PCM_I32 上位 24bit 詰め（数学的等価） |
| デジタルボリューム禁止 | アプリ内ボリューム処理なし |
| MQA | タグ検出 + ES9219C HW レンダラーへパススルー |
| Jitter 対策 | ハードウェア（FPGA + KDS 水晶）に委ねる |

---

## アプリ情報

| 項目 | 値 |
|---|---|
| アプリ名 | ZEROnOisE |
| パッケージ | com.zeronoise.player |
| 対象デバイス | Shanling M3 Ultra (Android 10) |
| アーキテクチャ | arm64-v8a |
