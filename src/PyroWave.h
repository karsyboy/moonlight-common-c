// Private Sunshine/Moonlight protocol extension. No PyroWave library dependency.
#pragma once

#include "Limelight.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PYROWAVE_STREAM_VERSION 1
#define PYROWAVE_MAX_FRAME_BYTES (3u * 1024u * 1024u)
#define PYROWAVE_MAX_BITRATE_KBPS 2000000

// Select only with both HTTP capabilities and the exact SDP wire version.
static inline int LiSelectPyroWaveFormat(int formats, int modes, const char* sdp) {
    if (!(formats & VIDEO_FORMAT_MASK_PYROWAVE) || !sdp ||
        !strstr(sdp, "a=x-ss-pyrowave.version:1\r\n")) return 0;
    int selected = 0;
    if ((formats & VIDEO_FORMAT_PYROWAVE_444) && (modes & SCM_PYROWAVE_444))
        selected = VIDEO_FORMAT_PYROWAVE_444;
    else if ((formats & VIDEO_FORMAT_PYROWAVE) && (modes & SCM_PYROWAVE))
        selected = VIDEO_FORMAT_PYROWAVE;
    if ((formats & VIDEO_FORMAT_PYROWAVE_HDR) && !(modes & SCM_PYROWAVE_HDR)) return 0;
    return selected ? selected | (formats & VIDEO_FORMAT_PYROWAVE_HDR) : 0;
}

// PyroWave expects bytes per frame, aligned down to a 32-bit word.
static inline size_t LiPyroWaveFrameBudget(int bitrateKbps, int fps) {
    if (bitrateKbps <= 0 || bitrateKbps > PYROWAVE_MAX_BITRATE_KBPS || fps <= 0 || fps > 240) return 0;
    uint64_t bytes = (uint64_t)bitrateKbps * 1000 / ((uint64_t)fps * 8);
    bytes &= ~(uint64_t)3;
    return bytes >= 1024 && bytes <= PYROWAVE_MAX_FRAME_BYTES - 8 ? (size_t)bytes : 0;
}
