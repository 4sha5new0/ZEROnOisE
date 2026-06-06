# ─────────────────────────────────────────────────────────────────────────────
# ZEROnOisE APK ビルド用 Dockerfile
#
# 使い方:
#   docker build -t audioplayer-builder .
#   docker run --rm -v "$(pwd)/apk-output:/output" audioplayer-builder
#   → apk-output/app-debug.apk が生成される
# ─────────────────────────────────────────────────────────────────────────────
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
ENV ANDROID_HOME=/opt/android-sdk
ENV ANDROID_SDK_ROOT=/opt/android-sdk
ENV NDK_VERSION=26.3.11579264
ENV BUILD_TOOLS_VERSION=34.0.0
ENV PATH=$PATH:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/ndk/$NDK_VERSION

# ── 依存パッケージ ────────────────────────────────────────────────────────────
RUN apt-get update && apt-get install -y --no-install-recommends \
    openjdk-17-jdk-headless \
    wget unzip git curl \
    python3 \
    ninja-build \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# ── Android SDK command-line tools ───────────────────────────────────────────
RUN mkdir -p $ANDROID_HOME/cmdline-tools && \
    wget -q https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip \
        -O /tmp/cmdtools.zip && \
    unzip -q /tmp/cmdtools.zip -d $ANDROID_HOME/cmdline-tools && \
    mv $ANDROID_HOME/cmdline-tools/cmdline-tools $ANDROID_HOME/cmdline-tools/latest && \
    rm /tmp/cmdtools.zip

# ── SDK コンポーネントのインストール ──────────────────────────────────────────
RUN yes | sdkmanager --licenses > /dev/null 2>&1 ; \
    sdkmanager \
        "platform-tools" \
        "platforms;android-29" \
        "build-tools;${BUILD_TOOLS_VERSION}" \
        "ndk;${NDK_VERSION}" \
        "cmake;3.22.1"

# ── プロジェクトをコピー ──────────────────────────────────────────────────────
WORKDIR /project
COPY . .

# ── local.properties 生成 ────────────────────────────────────────────────────
RUN echo "sdk.dir=${ANDROID_HOME}" > local.properties && \
    echo "ndk.dir=${ANDROID_HOME}/ndk/${NDK_VERSION}" >> local.properties && \
    chmod +x gradlew

# ── APK ビルド ────────────────────────────────────────────────────────────────
RUN ./gradlew assembleDebug \
        -Pandroid.ndkVersion=${NDK_VERSION} \
        --no-daemon \
        --stacktrace

# ── APK を /output にコピーするエントリーポイント ─────────────────────────────
CMD ["sh", "-c", \
    "mkdir -p /output && \
     cp app/build/outputs/apk/debug/app-debug.apk /output/ZEROnOisE-debug.apk && \
     echo '✅  APK: /output/ZEROnOisE-debug.apk'"]
