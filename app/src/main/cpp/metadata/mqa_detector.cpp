#include "mqa_detector.h"
#include <algorithm>
#include <cctype>

std::string MQADetector::ToUpper(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
        [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    return r;
}

void MQADetector::Detect(AudioInfo& info, const TagMap& tags) {
    const auto enc_it = tags.find("MQAENCODER");
    if (enc_it == tags.end()) return;

    info.is_mqa     = true;
    info.mqa_encoder = enc_it->second;

    const auto sr_it = tags.find("ORIGINALSAMPLERATE");
    if (sr_it != tags.end()) {
        try {
            info.mqa_original_sr = static_cast<uint32_t>(std::stoul(sr_it->second));
            info.is_mqa_studio   = (info.mqa_original_sr > 0);
        } catch (...) {}
    }
}
