// Audio quality request (x-moonshine-audio.quality): host advertisement
// parsing, the requested level and the generated ANNOUNCE attributes.
#include "AudioQuality.h"
#include "Limelight-internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int sdpHas(const char* attribute) {
    int length;
    char* payload = getSdpPayloadForStreamConfig(14, &length);
    assert(payload && length > 0);
    int found = strstr(payload, attribute) != NULL;
    free(payload);
    return found;
}

static void test_host_level(void) {
    assert(LiHostAudioQualityLevel(NULL) == -1);
    assert(LiHostAudioQualityLevel("v=0\r\n") == -1);
    assert(LiHostAudioQualityLevel("a=x-moonshine-audio.quality:2\r\n") == 2);
    assert(LiHostAudioQualityLevel("a=x-moonshine-audio.quality: 0 \n") == 0);
    assert(LiHostAudioQualityLevel("a=x-moonshine-audio.quality:\r\n") == -1);
    assert(LiHostAudioQualityLevel("a=x-moonshine-audio.quality:max\r\n") == -1);
    assert(LiHostAudioQualityLevel("a=x-moonshine-audio.quality:-1\r\n") == -1);
    assert(LiHostAudioQualityLevel("a=x-moonshine-audio.quality:123456789\r\n") == -1);
    assert(LiHostAudioQualityLevel("a=x-moonshine-audio.quality:1\na=x-moonshine-audio.quality:2\n") == -1);
    // Only complete attribute names match.
    assert(LiHostAudioQualityLevel("a=x-moonshine-audio.qualityLevel:2\r\n") == -1);
}

static void test_request(void) {
    assert(LiAudioQualityRequest(AUDIO_QUALITY_HOST_DEFAULT, 2) == -1);
    assert(LiAudioQualityRequest(AUDIO_QUALITY_STANDARD, 2) == 0);
    assert(LiAudioQualityRequest(AUDIO_QUALITY_HIGH, 2) == 1);
    assert(LiAudioQualityRequest(AUDIO_QUALITY_MAXIMUM, 2) == 2);
    assert(LiAudioQualityRequest(AUDIO_QUALITY_MAXIMUM, 1) == 1); // Older host.
    assert(LiAudioQualityRequest(AUDIO_QUALITY_MAXIMUM, -1) == -1); // Host without the extension.
}

// The level is sent only to a host that advertised it, for every video codec;
// stereo never selects the high-quality (different-layout) Opus configuration.
static void test_announce(void) {
    LiInitializeStreamConfiguration(&StreamConfig);
    StreamConfig.width = 1920; StreamConfig.height = 1080; StreamConfig.fps = 60;
    StreamConfig.packetSize = 1392;
    StreamConfig.bitrate = 5000; // Below Moonlight's high-quality surround threshold.
    StreamConfig.streamingRemotely = STREAM_CFG_LOCAL;
    AppVersionQuad[0] = 7; AppVersionQuad[1] = 1; AppVersionQuad[2] = 431; AppVersionQuad[3] = -1;
    ((struct sockaddr_in*)&RemoteAddr)->sin_family = AF_INET;
    ((struct sockaddr_in*)&RemoteAddr)->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    RtspPortNumber = 48010; VideoPortNumber = 47998;

    const int formats[] = {VIDEO_FORMAT_H264, VIDEO_FORMAT_H265, VIDEO_FORMAT_AV1_MAIN8};
    for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        NegotiatedVideoFormat = formats[i];
        StreamConfig.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
        HostAudioQualityLevel = 2;
        StreamConfig.audioQuality = AUDIO_QUALITY_HOST_DEFAULT;
        assert(!sdpHas("x-moonshine-audio.quality"));
        StreamConfig.audioQuality = AUDIO_QUALITY_MAXIMUM;
        assert(sdpHas("a=x-moonshine-audio.quality:2 \r\n"));
        assert(sdpHas("a=x-nv-audio.surround.AudioQuality:0 \r\n") && !HighQualitySurroundEnabled);
        HostAudioQualityLevel = -1;
        assert(!sdpHas("x-moonshine-audio.quality"));

        StreamConfig.audioConfiguration = AUDIO_CONFIGURATION_51_SURROUND;
        HighQualitySurroundSupported = true;
        StreamConfig.audioQuality = AUDIO_QUALITY_STANDARD;
        assert(sdpHas("a=x-nv-audio.surround.AudioQuality:0 \r\n") && !HighQualitySurroundEnabled);
        StreamConfig.audioQuality = AUDIO_QUALITY_HIGH; // Any host, any video bitrate.
        assert(sdpHas("a=x-nv-audio.surround.AudioQuality:1 \r\n") && HighQualitySurroundEnabled);
        assert(AudioPacketDuration == 5);
        AudioCallbacks.capabilities = CAPABILITY_SLOW_OPUS_DECODER;
        assert(sdpHas("a=x-nv-audio.surround.AudioQuality:0 \r\n") && !HighQualitySurroundEnabled);
        AudioCallbacks.capabilities = 0;
        HighQualitySurroundSupported = false;
        assert(sdpHas("a=x-nv-audio.surround.AudioQuality:0 \r\n") && !HighQualitySurroundEnabled);
    }
}

int main(void) {
    test_host_level();
    test_request();
    test_announce();
    puts("Audio quality host level, request and H264/HEVC/AV1 ANNOUNCE tests passed");
    return 0;
}
