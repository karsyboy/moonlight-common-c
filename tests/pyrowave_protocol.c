#include "PyroWave.h"
#include "Limelight-internal.h"
#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static unsigned delivered;
static const unsigned char payload[2000] = {0, 0, 1, 0x67, 0, 0, 1, 0x68};
static const unsigned char* expectedPayload = payload;
static size_t expectedPayloadSize = sizeof(payload);
static int submit(PDECODE_UNIT du) {
    assert(du->fullLength == (int)expectedPayloadSize);
    size_t offset = 0;
    for (PLENTRY e = du->bufferList; e; e = e->next) {
        assert(e->bufferType == BUFFER_TYPE_PICDATA);
        assert(e->length <= expectedPayloadSize - offset);
        assert(!memcmp(e->data, expectedPayload + offset, e->length));
        offset += e->length;
    }
    assert(offset == expectedPayloadSize); delivered++;
    return DR_OK;
}
static void send_packet(unsigned frame, unsigned spi, bool first) {
    const size_t bytes = sizeof(RTP_PACKET) + sizeof(NV_VIDEO_PACKET) + 1376;
    const size_t entryOffset = (bytes + 15) & ~(size_t)15;
    char *allocation = calloc(1, entryOffset + sizeof(RTPV_QUEUE_ENTRY));
    assert(allocation);
    PRTP_PACKET rtp = (PRTP_PACKET)allocation;
    PNV_VIDEO_PACKET nv = (PNV_VIDEO_PACKET)(rtp + 1);
    nv->frameIndex = frame; nv->streamPacketIndex = spi << 8;
    nv->flags = FLAG_CONTAINS_PIC_DATA | (first ? FLAG_SOF : FLAG_EOF);
    char *data = (char*)(nv + 1);
    if (first) {
        data[0] = 1; data[3] = 2;
        const unsigned last = sizeof(payload) - 1368;
        data[4] = last & 255; data[5] = last >> 8;
        memcpy(data + 8, payload, 1368);
    }
    else memcpy(data, payload + 1368, sizeof(payload) - 1368);
    PRTPV_QUEUE_ENTRY entry = (PRTPV_QUEUE_ENTRY)(allocation + entryOffset);
    entry->packet = rtp; entry->length = bytes;
    entry->receiveTimeUs = 100000 + frame * 16667;
    queueRtpPacket(entry);
}
static void test_reassembly(void) {
    NegotiatedVideoFormat = VIDEO_FORMAT_PYROWAVE_444 | VIDEO_FORMAT_PYROWAVE_HDR;
    AppVersionQuad[2] = 450;
    VideoCallbacks.capabilities = CAPABILITY_DIRECT_SUBMIT;
    VideoCallbacks.submitDecodeUnit = submit;
    initializeVideoDepacketizer(1392);
    unsigned spi = 0;
    for (unsigned frame = 1; frame <= 12; frame++) {
        if (frame == 4) { notifyFrameLost(frame, false); spi += 2; continue; }
        send_packet(frame, spi++, true);
        send_packet(frame, spi++, false);
    }
    assert(delivered == 11);
    destroyVideoDepacketizer();
}

static void send_large_frame(unsigned frame, unsigned* spi, const unsigned char* framePayload,
                             size_t framePayloadSize, unsigned packetCount) {
    const size_t shardPayloadSize = 1376;
    assert(framePayloadSize + 8 == (size_t)packetCount * shardPayloadSize);
    for (unsigned i = 0; i < packetCount; i++) {
        const size_t bytes = sizeof(RTP_PACKET) + sizeof(NV_VIDEO_PACKET) + shardPayloadSize;
        const size_t entryOffset = (bytes + 15) & ~(size_t)15;
        char* allocation = calloc(1, entryOffset + sizeof(RTPV_QUEUE_ENTRY));
        assert(allocation);
        PRTP_PACKET rtp = (PRTP_PACKET)allocation;
        PNV_VIDEO_PACKET nv = (PNV_VIDEO_PACKET)(rtp + 1);
        nv->frameIndex = frame;
        nv->streamPacketIndex = (*spi)++ << 8;
        nv->flags = FLAG_CONTAINS_PIC_DATA |
                    (i == 0 ? FLAG_SOF : 0) |
                    (i + 1 == packetCount ? FLAG_EOF : 0);
        char* data = (char*)(nv + 1);
        const size_t logicalOffset = (size_t)i * shardPayloadSize;
        if (i == 0) {
            data[0] = 1;
            data[3] = 2;
            data[4] = shardPayloadSize & 255;
            data[5] = shardPayloadSize >> 8;
            memcpy(data + 8, framePayload, shardPayloadSize - 8);
        }
        else {
            memcpy(data, framePayload + logicalOffset - 8, shardPayloadSize);
        }
        PRTPV_QUEUE_ENTRY entry = (PRTPV_QUEUE_ENTRY)(allocation + entryOffset);
        entry->packet = rtp;
        entry->length = bytes;
        entry->receiveTimeUs = 100000 + frame * 8333;
        queueRtpPacket(entry);
    }
}

static void test_large_packet_count_reassembly(void) {
    NegotiatedVideoFormat = VIDEO_FORMAT_PYROWAVE;
    AppVersionQuad[2] = 450;
    VideoCallbacks.capabilities = CAPABILITY_DIRECT_SUBMIT;
    VideoCallbacks.submitDecodeUnit = submit;
    initializeVideoDepacketizer(1392);
    unsigned spi = 0;
    unsigned frame = 1;
    const unsigned before = delivered;
    // 276 = 6 GSO chunks, 322 = exactly 7, 323 = 7 + 1 shard,
    // and 368 = exactly 8 with the host's 46-segment GSO payload cap.
    const unsigned packetCounts[] = {276, 322, 323, 368, 361};
    for (unsigned i = 0; i < sizeof(packetCounts) / sizeof(packetCounts[0]); i++, frame++) {
        const size_t size = (size_t)packetCounts[i] * 1376 - 8;
        unsigned char* largePayload = malloc(size);
        assert(largePayload);
        for (size_t j = 0; j < size; j++) largePayload[j] = (unsigned char)(j * 131u + frame);
        expectedPayload = largePayload;
        expectedPayloadSize = size;
        send_large_frame(frame, &spi, largePayload, size, packetCounts[i]);
        free(largePayload);
    }
    expectedPayload = payload;
    expectedPayloadSize = sizeof(payload);
    assert(delivered == before + sizeof(packetCounts) / sizeof(packetCounts[0]));
    destroyVideoDepacketizer();
}

int main(void) {
    const char *v1 = "v=0\r\na=x-ss-pyrowave.version:1\r\n";
    const int modes = SCM_MASK_PYROWAVE | SCM_H264 | SCM_HEVC | SCM_AV1_MAIN8;
    const int legacy = VIDEO_FORMAT_MASK_H264 | VIDEO_FORMAT_MASK_H265 | VIDEO_FORMAT_MASK_AV1;
    assert(!(legacy & (VIDEO_FORMAT_MASK_PYROWAVE | VIDEO_FORMAT_PYROWAVE_HDR)));
    assert(!(SCM_MASK_PYROWAVE & (SCM_MASK_H264 | SCM_MASK_HEVC | SCM_MASK_AV1)));
    assert(LiSelectPyroWaveFormat(legacy, modes, v1) == 0); // Automatic stays conventional.
    assert(LiSelectPyroWaveFormat(VIDEO_FORMAT_PYROWAVE, 0, v1) == 0); // Old server.
    assert(LiSelectPyroWaveFormat(VIDEO_FORMAT_PYROWAVE, modes, NULL) == 0);
    assert(LiSelectPyroWaveFormat(VIDEO_FORMAT_PYROWAVE, modes, "a=x-ss-pyrowave.version:2\r\n") == 0);
    assert(LiSelectPyroWaveFormat(VIDEO_FORMAT_PYROWAVE, modes, "a=x-ss-pyrowave.version:10\r\n") == 0);
    for (int hdr = 0; hdr <= 1; ++hdr) for (int c444 = 0; c444 <= 1; ++c444) {
        const int f = (c444 ? VIDEO_FORMAT_PYROWAVE_444 : VIDEO_FORMAT_PYROWAVE) | (hdr ? VIDEO_FORMAT_PYROWAVE_HDR : 0);
        assert(LiSelectPyroWaveFormat(f, modes, v1) == f);
        if (hdr) assert(LiSelectPyroWaveFormat(f, modes & ~SCM_PYROWAVE_HDR, v1) == 0);
    }
    assert(LiSelectPyroWaveFormat(VIDEO_FORMAT_MASK_PYROWAVE, SCM_PYROWAVE, v1) == VIDEO_FORMAT_PYROWAVE);
    const char *records = "a=rtpmap:99 PYROWAVE/90000\r\na=x-ss-pyrowave.bitstream:186f0393\r\na=x-ss-pyrowave.dialects:record-framed\r\n";
    const char *both = "a=rtpmap:99 PYROWAVE/90000\r\na=x-ss-pyrowave.bitstream:186f0393\r\na=x-ss-pyrowave.version:1\r\na=x-ss-pyrowave.dialects:native-wire-v1 record-framed\r\n";
    assert(LiNegotiatePyroWave(VIDEO_FORMAT_PYROWAVE, modes, both).dialect == PYROWAVE_DIALECT_NATIVE_WIRE_V1);
    for (int hdr = 0; hdr <= 1; ++hdr) for (int c444 = 0; c444 <= 1; ++c444) {
        const int f = (c444 ? VIDEO_FORMAT_PYROWAVE_444 : VIDEO_FORMAT_PYROWAVE) | (hdr ? VIDEO_FORMAT_PYROWAVE_HDR : 0);
        PYROWAVE_NEGOTIATION n = LiNegotiatePyroWave(f, modes | SCM_PYROWAVE_RECORD_HDR444, records);
        assert(n.dialect == PYROWAVE_DIALECT_RECORD_FRAMED && n.format == f);
        PYROWAVE_VIDEO_PROFILE profile = LiPyroWaveProfile(n.format);
        assert(profile.chroma444 == c444 && profile.hdr10 == hdr && profile.bitDepth == (hdr ? 10 : 8));
        if (hdr && c444) assert(!LiSelectPyroWaveFormat(f, modes, records));
    }
    const char *invalid[] = {
        "a=x-ss-pyrowave.version:1\na=x-ss-pyrowave.version:1\n",
        "a=x-ss-pyrowave.version:\n",
        "a=x-ss-pyrowave.version:1\na=x-ss-pyrowave.dialects:record-framed\n",
        "a=rtpmap:99 PYROWAVE/90000-bogus\na=x-ss-pyrowave.bitstream:186f0393\n",
        "a=rtpmap:99 PYROWAVE/90000\na=x-ss-pyrowave.bitstream:unknown\n",
        "a=x-ss-pyrowave.version:1\na=x-ss-pyrowave.profiles:444-hdr10\n",
    };
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        PYROWAVE_NEGOTIATION n = LiNegotiatePyroWave(VIDEO_FORMAT_PYROWAVE, modes, invalid[i]);
        assert(!n.format && n.error);
    }
    assert(LiSelectPyroWaveFormat(VIDEO_FORMAT_PYROWAVE, modes, "a=x-ss-pyrowave.version:1 \r\n") == VIDEO_FORMAT_PYROWAVE);
    assert(LiPyroWaveFrameBudget(200000, 60) == 416664);
    assert(LiPyroWaveFrameBudget(1000000, 120) == 1041664);
    assert(LiPyroWaveFrameBudget(2000000, 120) == 2083332);
    assert(LiPyroWaveFrameBudget(2000000, 60) == 0); // Transport frame bound.
    const int boundaryRatesKbps[] = {400000, 424000, 425000, 426000, 500000, 750000, 1000000, 2000000};
    for (unsigned i = 0; i < sizeof(boundaryRatesKbps) / sizeof(boundaryRatesKbps[0]); i++) {
        const uint64_t expected = ((uint64_t)boundaryRatesKbps[i] * 1000 / (120 * 8)) & ~(uint64_t)3;
        assert(LiPyroWaveFrameBudget(boundaryRatesKbps[i], 120) == expected);
    }
    assert(LiPyroWaveFrameBudget(INT_MAX, 60) == 0);
    assert(LiPyroWaveFrameBudget(-1, 60) == 0);
    assert(LiPyroWaveFrameBudget(200000, 0) == 0);
    LiInitializeStreamConfiguration(&StreamConfig);
    StreamConfig.width = 1920; StreamConfig.height = 1080; StreamConfig.fps = 60;
    StreamConfig.packetSize = 1392; StreamConfig.bitrate = 200000;
    StreamConfig.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
    StreamConfig.streamingRemotely = STREAM_CFG_LOCAL;
    AppVersionQuad[0] = 7; AppVersionQuad[1] = 1; AppVersionQuad[2] = 431; AppVersionQuad[3] = -1;
    ((struct sockaddr_in*)&RemoteAddr)->sin_family = AF_INET;
    ((struct sockaddr_in*)&RemoteAddr)->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    RtspPortNumber = 48010; VideoPortNumber = 47998;
    const int formats[] = {VIDEO_FORMAT_H264, VIDEO_FORMAT_H265, VIDEO_FORMAT_AV1_MAIN8,
                          VIDEO_FORMAT_PYROWAVE, VIDEO_FORMAT_PYROWAVE_444 | VIDEO_FORMAT_PYROWAVE_HDR};
    for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        NegotiatedVideoFormat = formats[i]; int length;
        char* payload = getSdpPayloadForStreamConfig(14, &length);
        assert(payload && length > 0);
        char expected[80];
        snprintf(expected, sizeof(expected), "a=x-nv-vqos[0].bitStreamFormat:%u \r\n", i < 3 ? i : 3);
        assert(strstr(payload, expected));
        assert(!!strstr(payload, "a=x-ss-pyrowave.version:1 \r\n") == (i >= 3));
        if (i == 4) {
            assert(strstr(payload, "a=x-ss-video[0].chromaSamplingType:1 \r\n"));
            assert(strstr(payload, "a=x-nv-video[0].dynamicRangeMode:1 \r\n"));
        }
        free(payload);
    }
    test_reassembly();
    test_large_packet_count_reassembly();
    puts("H264/HEVC/AV1/PyroWave SDP + HDR/444/rate/reassembly/loss tests passed");
}
