#include "metadata_reader.h"
#include "../decoder/flac_decoder.h"
#include "../decoder/wav_decoder.h"
#include "../decoder/mp3_decoder.h"
#include "../decoder/mediacodec_decoder.h"
#include <memory>     // std::make_unique
#include <cctype>
#include <algorithm>

static std::string ToLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return r;
}

AudioFormat MetadataReader::DetectFormat(const std::string& path) {
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos) return AudioFormat::UNKNOWN;
    const std::string ext = ToLower(path.substr(dot + 1));
    if (ext == "flac")              return AudioFormat::FLAC;
    if (ext == "wav" || ext == "wave") return AudioFormat::WAV;
    if (ext == "mp3")               return AudioFormat::MP3;
    if (ext == "aac" || ext == "m4a") return AudioFormat::AAC;
    if (ext == "webm")              return AudioFormat::WEBM;
    return AudioFormat::UNKNOWN;
}

AudioInfo MetadataReader::Read(const std::string& path) {
    const AudioFormat fmt = DetectFormat(path);
    AudioInfo info;
    info.path = path;

    std::unique_ptr<DecoderBase> dec;
    switch (fmt) {
        case AudioFormat::FLAC: dec = std::make_unique<FlacDecoder>(); break;
        case AudioFormat::WAV:  dec = std::make_unique<WavDecoder>();  break;
        case AudioFormat::MP3:  dec = std::make_unique<Mp3Decoder>();  break;
        case AudioFormat::AAC:
        case AudioFormat::WEBM: dec = std::make_unique<MediaCodecDecoder>(); break;
        default: return info;
    }

    if (dec->Open(path)) {
        info = dec->GetInfo();
    }
    return info;
}
