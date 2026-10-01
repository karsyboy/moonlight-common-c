// Private Sunshine/Moonlight protocol extension. No PyroWave library dependency.
#pragma once

#include "Limelight.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PYROWAVE_STREAM_VERSION 1
#define RTP_RECV_PACKETS_BUFFERED 2048
#define PYROWAVE_RECV_PACKETS_BUFFERED 8192
#define PYROWAVE_RECOMMENDED_RMEM_MAX (32LL * 1024 * 1024)

// packetSize is negotiated; RTP permits a 16-byte header. Zero means invalid.
static inline int LiVideoReceiveBufferSize(int packetSize, int pyrowave) {
    int packets = pyrowave ? PYROWAVE_RECV_PACKETS_BUFFERED : RTP_RECV_PACKETS_BUFFERED;
    if (packetSize <= 0 || packetSize > INT32_MAX / packets - 16) return 0;
    return packets * (packetSize + 16);
}

typedef enum _PYROWAVE_RECEIVE_LIMIT_STATUS {
    PYROWAVE_RECEIVE_LIMIT_UNKNOWN,
    PYROWAVE_RECEIVE_LIMIT_ADEQUATE,
    PYROWAVE_RECEIVE_LIMIT_INADEQUATE
} PYROWAVE_RECEIVE_LIMIT_STATUS;
static inline PYROWAVE_RECEIVE_LIMIT_STATUS LiPyroWaveReceiveLimitStatus(int64_t limit, int packetSize) {
    int required = LiVideoReceiveBufferSize(packetSize, 1);
    if (limit <= 0 || !required) return PYROWAVE_RECEIVE_LIMIT_UNKNOWN;
    return limit < required ? PYROWAVE_RECEIVE_LIMIT_INADEQUATE : PYROWAVE_RECEIVE_LIMIT_ADEQUATE;
}

#define PYROWAVE_MAX_FRAME_BYTES (3u * 1024u * 1024u)
#define PYROWAVE_MAX_BITRATE_KBPS 2000000

// The block format was checked in both directions against Nonary's vendored
// 186f0393 codec. This ID describes the bitstream family, not the C API ABI.
#define PYROWAVE_BITSTREAM_ID "186f0393"
#define SCM_PYROWAVE_RECORD_HDR444 0x04000000

typedef struct _PYROWAVE_NEGOTIATION {
    PYROWAVE_DIALECT dialect;
    int format;
    const char* error;
} PYROWAVE_NEGOTIATION;

// Match complete SDP lines; duplicate/conflicting attributes are rejected.
static inline int LiPyroWaveSdpValue(const char* sdp, const char* key, char* out, size_t capacity) {
    int found = 0;
    size_t klen = strlen(key);
    for (const char* line = sdp; line && *line;) {
        const char* end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        if (length && line[length - 1] == '\r') --length;
        if (length >= klen + 3 && !memcmp(line, "a=", 2) &&
            !memcmp(line + 2, key, klen) && line[2 + klen] == ':') {
            const char* value = line + klen + 3;
            size_t valueLength = length - klen - 3;
            while (valueLength && (*value == ' ' || *value == '\t')) { ++value; --valueLength; }
            while (valueLength && (value[valueLength-1] == ' ' || value[valueLength-1] == '\t')) --valueLength;
            if (!valueLength) return -1;
            if (++found > 1 || valueLength >= capacity) return -1;
            memcpy(out, value, valueLength); out[valueLength] = 0;
        }
        line = end ? end + 1 : NULL;
    }
    return found;
}

// Space-separated setup capability tokens, never substring guesses.
static inline int LiPyroWaveHasToken(const char* list, const char* token) {
    size_t length = strlen(token);
    for (const char* pos = list; *pos;) {
        while (*pos == ' ' || *pos == '\t') ++pos;
        const char* end = pos;
        while (*end && *end != ' ' && *end != '\t') ++end;
        if ((size_t)(end - pos) == length && !memcmp(pos, token, length)) return 1;
        pos = end;
    }
    return 0;
}
static inline int LiPyroWaveHasRtpMap(const char* sdp) {
    const char* expected = "a=rtpmap:99 PYROWAVE/90000";
    size_t length = strlen(expected);
    for (const char* line = sdp; line && *line;) {
        const char* end = strchr(line, '\n');
        size_t bytes = end ? (size_t)(end-line) : strlen(line);
        while (bytes && (line[bytes-1] == '\r' || line[bytes-1] == ' ')) --bytes;
        if (bytes == length && !memcmp(line, expected, length)) return 1;
        line = end ? end+1 : NULL;
    }
    return 0;
}
// Wire bits terminate here. All downstream code uses local video format axes.
typedef struct _PYROWAVE_VIDEO_PROFILE {
    int chroma444;
    int bitDepth;
    int hdr10;
} PYROWAVE_VIDEO_PROFILE;
static inline PYROWAVE_VIDEO_PROFILE LiPyroWaveProfile(int format) {
    PYROWAVE_VIDEO_PROFILE p = {!!(format & VIDEO_FORMAT_PYROWAVE_444),
        format & VIDEO_FORMAT_PYROWAVE_HDR ? 10 : 8, !!(format & VIDEO_FORMAT_PYROWAVE_HDR)};
    return p;
}

static inline PYROWAVE_NEGOTIATION LiNegotiatePyroWave(int formats, int modes, const char* sdp) {
    PYROWAVE_NEGOTIATION n = {PYROWAVE_DIALECT_NONE, 0, NULL};
    if (!(formats & VIDEO_FORMAT_MASK_PYROWAVE) || !sdp) return n;
    char version[32] = {0}, bitstream[32] = {0}, dialects[96] = {0}, profiles[128] = {0};
    int v = LiPyroWaveSdpValue(sdp, "x-ss-pyrowave.version", version, sizeof(version));
    int b = LiPyroWaveSdpValue(sdp, "x-ss-pyrowave.bitstream", bitstream, sizeof(bitstream));
    int d = LiPyroWaveSdpValue(sdp, "x-ss-pyrowave.dialects", dialects, sizeof(dialects));
    int p = LiPyroWaveSdpValue(sdp, "x-ss-pyrowave.profiles", profiles, sizeof(profiles));
    if (v < 0 || b < 0 || d < 0 || p < 0) { n.error = "Duplicate or oversized PyroWave capability attribute"; return n; }
    if (b && strcmp(bitstream, PYROWAVE_BITSTREAM_ID)) {
        n.error = "PyroWave bitstream revision incompatible (client requires 186f0393)"; return n;
    }
    if (v) {
        if (strcmp(version, "1")) { n.error = "Unsupported PyroWave native wire version"; return n; }
        if (d && !LiPyroWaveHasToken(dialects, "native-wire-v1")) {
            n.error = "Contradictory PyroWave native version and dialect advertisement"; return n;
        }
        n.dialect = PYROWAVE_DIALECT_NATIVE_WIRE_V1;
    } else if (b && LiPyroWaveHasRtpMap(sdp) && (!d || LiPyroWaveHasToken(dialects, "record-framed"))) {
        // Vibepollo's documented default for clients announcing features bit 1.
        n.dialect = PYROWAVE_DIALECT_RECORD_FRAMED;
    } else { n.error = "Host does not advertise a supported PyroWave dialect and bitstream revision"; return n; }
    int hdr = formats & VIDEO_FORMAT_PYROWAVE_HDR;
    if ((formats & VIDEO_FORMAT_PYROWAVE_444) && (modes & SCM_PYROWAVE_444)) {
        int hdrBit = n.dialect == PYROWAVE_DIALECT_NATIVE_WIRE_V1 ? SCM_PYROWAVE_HDR : SCM_PYROWAVE_RECORD_HDR444;
        if ((!hdr || (modes & hdrBit)) && (!p || LiPyroWaveHasToken(profiles, hdr ? "444-hdr10" : "444-sdr8"))) n.format = VIDEO_FORMAT_PYROWAVE_444 | hdr;
    }
    if (!n.format && (formats & VIDEO_FORMAT_PYROWAVE) && (modes & SCM_PYROWAVE) && (!hdr || (modes & SCM_PYROWAVE_HDR)) && (!p || LiPyroWaveHasToken(profiles, hdr ? "420-hdr10" : "420-sdr8")))
        n.format = VIDEO_FORMAT_PYROWAVE | hdr;
    if (!n.format) n.error = "Host does not support the requested PyroWave chroma/HDR profile";
    return n;
}
static inline int LiSelectPyroWaveFormat(int formats, int modes, const char* sdp) {
    return LiNegotiatePyroWave(formats, modes, sdp).format;
}

// PyroWave expects bytes per frame, aligned down to a 32-bit word.
static inline size_t LiPyroWaveFrameBudget(int bitrateKbps, int fps) {
    if (bitrateKbps <= 0 || bitrateKbps > PYROWAVE_MAX_BITRATE_KBPS || fps <= 0 || fps > 240) return 0;
    uint64_t bytes = (uint64_t)bitrateKbps * 1000 / ((uint64_t)fps * 8);
    bytes &= ~(uint64_t)3;
    return bytes >= 1024 && bytes <= PYROWAVE_MAX_FRAME_BYTES - 8 ? (size_t)bytes : 0;
}
