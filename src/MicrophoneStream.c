// Client microphone: Apollo's microphone passthrough, the wire format of
// ClassicOldSong/moonlight-common-c's MicrophoneStream.c that Nightjar,
// Rubylight and Pyroshine hosts take.
//
// The host offers it in DESCRIBE with an "m=audio <port>" line followed by
// "a=rtpmap:96 opus/48000/1" and the microphone bit (SS_ENC_MICROPHONE) in
// x-ss-general.encryptionSupported. The client sets it up with
// SETUP streamid=mic, enables the bit in ANNOUNCE and then sends one UDP
// datagram per mono Opus packet to the port from SETUP:
//
//   flags (0), type (0x61), sequence (LE16), client milliseconds (LE32),
//   magic 0x12345678 (LE32), AES-128-CBC encrypted Opus packet
//
// The key is the launch's remote input key; the IV is the audio stream's,
// BE32(rikeyid + sequence) followed by zeros. The packet is padded the way
// the reference client pads it: to a whole block with bytes holding the pad
// length, then a full PKCS#7 block. Hosts never take the microphone in the
// clear, so without negotiated encryption nothing is sent.

#include "Limelight-internal.h"

#define MIC_HEADER_LENGTH 12

static SOCKET micSocket = INVALID_SOCKET;
static PPLT_CRYPTO_CONTEXT micEncryptionCtx = NULL;
static uint32_t micRiKeyId;
static uint16_t micSequenceNumber;
// Serializes sends with initialization and teardown: the application's
// capture thread sends while the connection may be stopping.
static PLT_MUTEX micLock;
static bool micLockInitialized;

int LiHostMicrophonePort(const char* describePayload) {
    const char* rtpmap;
    const char* line;
    int port = 0;

    if (describePayload == NULL) {
        return -1;
    }

    rtpmap = strstr(describePayload, "a=rtpmap:96 opus/48000/1");
    if (rtpmap == NULL) {
        rtpmap = strstr(describePayload, "a=rtpmap:96 opus/48000/2");
    }
    if (rtpmap == NULL) {
        return -1;
    }

    // The port is the one on the last "m=audio" line before the rtpmap.
    for (line = strstr(describePayload, "m=audio "); line != NULL && line < rtpmap;
         line = strstr(line + 1, "m=audio ")) {
        char* end;
        unsigned long value = strtoul(line + strlen("m=audio "), &end, 10);
        port = (end != line + strlen("m=audio ") && value > 0 && value <= 65535) ? (int)value : 0;
    }

    return port;
}

int buildMicrophonePacket(PPLT_CRYPTO_CONTEXT ctx, const unsigned char* key, uint32_t riKeyId,
                          uint16_t sequenceNumber, uint32_t timestampMs,
                          const unsigned char* opusData, int opusLength,
                          unsigned char* packet, int packetCapacity) {
    unsigned char plaintext[MAX_MIC_PACKET_SIZE];
    unsigned char iv[16] = { 0 };
    uint32_t ivSeq;
    uint32_t value;
    int paddedLength;
    int encryptedLength;

    if (opusData == NULL || opusLength <= 0 || opusLength > MAX_MIC_PACKET_SIZE - MIC_HEADER_LENGTH - 32) {
        return -1;
    }

    // First padding layer: up to a whole block, each byte holding the pad length.
    memcpy(plaintext, opusData, opusLength);
    paddedLength = ROUND_TO_PKCS7_PADDED_LEN(opusLength);
    memset(&plaintext[opusLength], paddedLength - opusLength, paddedLength - opusLength);

    // The second layer, a full PKCS#7 block, comes from the cipher.
    encryptedLength = paddedLength + 16;
    if (MIC_HEADER_LENGTH + encryptedLength > packetCapacity) {
        return -1;
    }

    ivSeq = BE32(riKeyId + sequenceNumber);
    memcpy(iv, &ivSeq, sizeof(ivSeq));
    if (!PltEncryptMessage(ctx, ALGORITHM_AES_CBC, CIPHER_FLAG_RESET_IV | CIPHER_FLAG_FINISH,
                           (unsigned char*)key, 16, iv, sizeof(iv), NULL, 0,
                           plaintext, paddedLength,
                           &packet[MIC_HEADER_LENGTH], &encryptedLength)) {
        return -1;
    }

    packet[0] = 0;
    packet[1] = MIC_PACKET_TYPE_OPUS;
    packet[2] = (unsigned char)(sequenceNumber & 0xFF);
    packet[3] = (unsigned char)(sequenceNumber >> 8);
    value = LE32(timestampMs);
    memcpy(&packet[4], &value, sizeof(value));
    value = LE32(MIC_PACKET_MAGIC);
    memcpy(&packet[8], &value, sizeof(value));

    return MIC_HEADER_LENGTH + encryptedLength;
}

static void ensureMicLock(void) {
    // Created once, from the connection thread, before any send can run.
    if (!micLockInitialized) {
        PltCreateMutex(&micLock);
        micLockInitialized = true;
    }
}

int initializeMicrophoneStream(void) {
    int err = 0;

    ensureMicLock();
    PltLockMutex(&micLock);

    if (micSocket != INVALID_SOCKET) {
        goto Exit;
    }

    micEncryptionCtx = PltCreateCryptoContext();
    if (micEncryptionCtx == NULL) {
        err = -1;
        goto Exit;
    }

    memcpy(&micRiKeyId, StreamConfig.remoteInputAesIv, sizeof(micRiKeyId));
    micRiKeyId = BE32(micRiKeyId);
    micSequenceNumber = 0;

    micSocket = bindUdpSocket(RemoteAddr.ss_family, &LocalAddr, AddrLen, 0, SOCK_QOS_TYPE_AUDIO);
    if (micSocket == INVALID_SOCKET) {
        err = LastSocketFail();
        PltDestroyCryptoContext(micEncryptionCtx);
        micEncryptionCtx = NULL;
    }

Exit:
    PltUnlockMutex(&micLock);
    return err;
}

void destroyMicrophoneStream(void) {
    if (!micLockInitialized) {
        return;
    }

    PltLockMutex(&micLock);
    if (micSocket != INVALID_SOCKET) {
        closeSocket(micSocket);
        micSocket = INVALID_SOCKET;
    }
    if (micEncryptionCtx != NULL) {
        PltDestroyCryptoContext(micEncryptionCtx);
        micEncryptionCtx = NULL;
    }
    micRiKeyId = 0;
    micSequenceNumber = 0;
    PltUnlockMutex(&micLock);
}

int LiSendMicrophoneOpusData(const unsigned char* opusData, int opusLength) {
    unsigned char packet[MAX_MIC_PACKET_SIZE];
    LC_SOCKADDR saddr;
    int packetLength;
    int err;

    if (!micLockInitialized) {
        return -1;
    }

    PltLockMutex(&micLock);
    if (micSocket == INVALID_SOCKET) {
        PltUnlockMutex(&micLock);
        return -1;
    }

    packetLength = buildMicrophonePacket(micEncryptionCtx, (unsigned char*)StreamConfig.remoteInputAesKey,
                                         micRiKeyId, micSequenceNumber, (uint32_t)PltGetMillis(),
                                         opusData, opusLength, packet, sizeof(packet));
    if (packetLength < 0) {
        PltUnlockMutex(&micLock);
        Limelog("MIC: Cannot send a %d byte packet\n", opusLength);
        return -1;
    }
    micSequenceNumber++;

    memcpy(&saddr, &RemoteAddr, sizeof(saddr));
    SET_PORT(&saddr, MicPortNumber);
    err = sendto(micSocket, (const char*)packet, packetLength, 0, (struct sockaddr*)&saddr, AddrLen);
    PltUnlockMutex(&micLock);

    return err < 0 ? LastSocketError() : err;
}

bool LiIsMicrophoneStreamActive(void) {
    bool active;

    if (!micLockInitialized) {
        return false;
    }

    PltLockMutex(&micLock);
    active = micSocket != INVALID_SOCKET;
    PltUnlockMutex(&micLock);
    return active;
}
