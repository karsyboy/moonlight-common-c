// Private Pyroshine extension: the client requests an audio quality level.
// Independent of the video codec. See docs/audio-quality.md.
#pragma once

#include "Limelight.h"
#include "SdpAttribute.h"

#define HOST_AUDIO_QUALITY_ATTRIBUTE "x-moonshine-audio.quality"

// The highest audio quality level the host accepts in ANNOUNCE
// (x-moonshine-audio.quality in DESCRIBE), or -1 without the extension or
// with a malformed or duplicated value.
static inline int LiHostAudioQualityLevel(const char* sdp) {
    char value[8];
    int level = 0;
    if (!sdp || LiSdpAttributeValue(sdp, HOST_AUDIO_QUALITY_ATTRIBUTE, value, sizeof(value)) != 1) return -1;
    for (const char* c = value; *c; ++c) {
        if (*c < '0' || *c > '9') return -1;
        level = level * 10 + (*c - '0');
    }
    return level;
}

// The x-moonshine-audio.quality level to send in ANNOUNCE for an
// AUDIO_QUALITY_* request, or -1 to send none (the host's default applies).
// A level above the host's highest is reduced to it.
static inline int LiAudioQualityRequest(int audioQuality, int hostLevel) {
    int level;
    if (hostLevel < 0 || audioQuality < AUDIO_QUALITY_STANDARD) return -1;
    level = audioQuality - AUDIO_QUALITY_STANDARD;
    return level < hostLevel ? level : hostLevel;
}
