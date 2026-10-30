// Client microphone (Apollo's microphone passthrough): the host's DESCRIBE
// offer, the encrypted datagram layout and the ANNOUNCE encryption bit.
#include "Limelight-internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static const unsigned char KEY[16] = "mic-key-16-bytes";

static void test_offer(void) {
    assert(LiHostMicrophonePort(NULL) == -1);
    assert(LiHostMicrophonePort("v=0\r\na=rtpmap:98 AV1/90000\r\n") == -1);
    // Pyroshine, Rubylight and Apollo: the port of the m=audio line before the rtpmap.
    assert(LiHostMicrophonePort("a=fmtp:97 surround-params=21101\r\n"
                                "m=audio 48001 RTP/AVP 96\r\n"
                                "a=rtpmap:96 opus/48000/1\r\n"
                                "a=fmtp:96 minptime=10;useinbandfec=1\r\n") == 48001);
    assert(LiHostMicrophonePort("m=audio 1 RTP/AVP 97\r\nm=audio 48123 RTP/AVP 96\r\n"
                                "a=rtpmap:96 opus/48000/2\r\n") == 48123);
    // An m=audio line after the rtpmap does not name the microphone's port.
    assert(LiHostMicrophonePort("a=rtpmap:96 opus/48000/1\r\nm=audio 5000 RTP/AVP 96\r\n") == 0);
    assert(LiHostMicrophonePort("m=audio 99999 RTP/AVP 96\r\na=rtpmap:96 opus/48000/1\r\n") == 0);
    assert(LiHostMicrophonePort("m=audio x RTP/AVP 96\r\na=rtpmap:96 opus/48000/1\r\n") == 0);
}

// The datagram decrypts under the launch key and IV to the Opus packet, padded
// to a block with pad-length bytes, then a full PKCS#7 block.
static void test_packet(void) {
    int lengths[] = { 1, 13, 15, 16, 17, 200, 1356 };
    for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        int length = lengths[i];
        unsigned char opus[1400];
        unsigned char packet[MAX_MIC_PACKET_SIZE];
        for (int j = 0; j < length; j++) {
            opus[j] = (unsigned char)(j * 7 + 1);
        }
        PPLT_CRYPTO_CONTEXT encrypt = PltCreateCryptoContext();
        int packetLength = buildMicrophonePacket(encrypt, KEY, 0xFFFFFFF0, 40, 1234, opus, length,
                                                 packet, sizeof(packet));
        PltDestroyCryptoContext(encrypt);
        int padded = ROUND_TO_PKCS7_PADDED_LEN(length);
        assert(packetLength == 12 + padded + 16);
        assert(packetLength <= MAX_MIC_PACKET_SIZE);
        assert(packet[0] == 0 && packet[1] == 0x61);
        assert(packet[2] == 40 && packet[3] == 0);
        assert(packet[4] == (1234 & 0xFF) && packet[5] == (1234 >> 8) && packet[6] == 0 && packet[7] == 0);
        assert(packet[8] == 0x78 && packet[9] == 0x56 && packet[10] == 0x34 && packet[11] == 0x12);

        // IV: BE32(rikeyid + sequence), wrapping: 0xFFFFFFF0 + 40 = 0x18.
        unsigned char iv[16] = { 0, 0, 0, 0x18 };
        unsigned char plaintext[1500];
        int plaintextLength = sizeof(plaintext);
        PPLT_CRYPTO_CONTEXT decrypt = PltCreateCryptoContext();
        assert(PltDecryptMessage(decrypt, ALGORITHM_AES_CBC, CIPHER_FLAG_RESET_IV | CIPHER_FLAG_FINISH,
                                 (unsigned char*)KEY, 16, iv, 16, NULL, 0,
                                 &packet[12], packetLength - 12, plaintext, &plaintextLength));
        PltDestroyCryptoContext(decrypt);
        assert(plaintextLength == padded);
        assert(memcmp(plaintext, opus, length) == 0);
        for (int j = length; j < padded; j++) {
            assert(plaintext[j] == padded - length);
        }
    }

    unsigned char packet[MAX_MIC_PACKET_SIZE];
    unsigned char big[1400] = { 0 };
    PPLT_CRYPTO_CONTEXT ctx = PltCreateCryptoContext();
    assert(buildMicrophonePacket(ctx, KEY, 0, 0, 0, big, 0, packet, sizeof(packet)) == -1);
    assert(buildMicrophonePacket(ctx, KEY, 0, 0, 0, big, 1357, packet, sizeof(packet)) == -1);
    assert(buildMicrophonePacket(ctx, KEY, 0, 0, 0, big, 100, packet, 100) == -1);
    PltDestroyCryptoContext(ctx);
}

static int sdpHas(const char* attribute) {
    int length;
    char* payload = getSdpPayloadForStreamConfig(14, &length);
    assert(payload && length > 0);
    int found = strstr(payload, attribute) != NULL;
    free(payload);
    return found;
}

// ANNOUNCE enables microphone encryption only for a microphone that was set up
// with a host that offers the encryption.
static void test_announce(void) {
    LiInitializeStreamConfiguration(&StreamConfig);
    StreamConfig.width = 1920; StreamConfig.height = 1080; StreamConfig.fps = 60;
    StreamConfig.packetSize = 1392;
    StreamConfig.bitrate = 20000;
    StreamConfig.streamingRemotely = STREAM_CFG_LOCAL;
    StreamConfig.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
    AppVersionQuad[0] = 7; AppVersionQuad[1] = 1; AppVersionQuad[2] = 431; AppVersionQuad[3] = -1;
    ((struct sockaddr_in*)&RemoteAddr)->sin_family = AF_INET;
    ((struct sockaddr_in*)&RemoteAddr)->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    RtspPortNumber = 48010; VideoPortNumber = 47998;
    NegotiatedVideoFormat = VIDEO_FORMAT_H265;
    HostAudioQualityLevel = -1;

    EncryptionFeaturesSupported = SS_ENC_CONTROL_V2 | SS_ENC_AUDIO | SS_ENC_MICROPHONE;
    MicPortNumber = 0;
    assert(sdpHas("a=x-ss-general.encryptionEnabled:1 \r\n"));
    MicPortNumber = 48001;
    assert(sdpHas("a=x-ss-general.encryptionEnabled:9 \r\n"));
    EncryptionFeaturesSupported = SS_ENC_CONTROL_V2 | SS_ENC_AUDIO;
    assert(sdpHas("a=x-ss-general.encryptionEnabled:1 \r\n"));
    MicPortNumber = 0;

    // Without a set-up stream nothing is sent.
    assert(!LiIsMicrophoneStreamActive());
    assert(LiSendMicrophoneOpusData((const unsigned char*)"x", 1) < 0);
}

int main(void) {
    test_offer();
    test_packet();
    test_announce();
    puts("Microphone offer, datagram and ANNOUNCE tests passed");
    return 0;
}
