// Replay detection of host-originated AES-GCM messages (control stream, RTSP):
// Pyroshine numbers control messages from one counter that only increases,
// so a channel sees increasing sequence numbers with gaps. Those must pass;
// a repeated or too old sequence number must not.
#include "Limelight-internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const unsigned char KEY[16] = "replay-key-16-by";

// A GCM message sealed with the control stream's host IV for `seq`.
typedef struct {
    unsigned char iv[12];
    unsigned char tag[16];
    unsigned char ciphertext[16];
} SEALED;

static SEALED seal(PPLT_CRYPTO_CONTEXT ctx, unsigned int seq) {
    SEALED sealed;
    unsigned char plaintext[16] = "control message";
    int length = sizeof(sealed.ciphertext);
    memset(sealed.iv, 0, sizeof(sealed.iv));
    memcpy(sealed.iv, &seq, sizeof(seq));
    sealed.iv[10] = 'H';
    sealed.iv[11] = 'C';
    assert(PltEncryptMessage(ctx, ALGORITHM_AES_GCM, 0, (unsigned char*)KEY, 16, sealed.iv, 12,
                             sealed.tag, 16, plaintext, sizeof(plaintext), sealed.ciphertext, &length));
    return sealed;
}

static bool open_on(PPLT_CRYPTO_CONTEXT ctx, unsigned char channel, unsigned int seq, int flags) {
    PPLT_CRYPTO_CONTEXT sealer = PltCreateCryptoContext(0);
    SEALED sealed = seal(sealer, seq);
    PltDestroyCryptoContext(sealer);
    unsigned char plaintext[16];
    int length = sizeof(plaintext);
    return PltDecryptMessage(ctx, ALGORITHM_AES_GCM, flags, channel, seq, (unsigned char*)KEY, 16,
                             sealed.iv, 12, sealed.tag, 16, sealed.ciphertext, sizeof(sealed.ciphertext),
                             plaintext, &length);
}

static bool open_seq(PPLT_CRYPTO_CONTEXT ctx, unsigned int seq) {
    return open_on(ctx, 0, seq, 0);
}

int main(void) {
    PPLT_CRYPTO_CONTEXT ctx = PltCreateCryptoContext(2);

    // Increasing with gaps, as one host counter shared by every message.
    unsigned int sequence[] = { 7, 8, 11, 40, 41, 300 };
    for (unsigned i = 0; i < sizeof(sequence) / sizeof(sequence[0]); i++) {
        assert(open_seq(ctx, sequence[i]));
    }
    // A replay of the newest or an older message is rejected.
    assert(!open_seq(ctx, 300));
    assert(!open_seq(ctx, 40));
    // An older message not yet seen (reordered) is accepted once.
    assert(open_seq(ctx, 299));
    assert(!open_seq(ctx, 299));
    // Older than the window: rejected even if never seen.
    assert(open_seq(ctx, 300 + REPLAY_WINDOW_SIZE + 10));
    assert(!open_seq(ctx, 301));
    // Channels keep separate windows.
    assert(open_on(ctx, 1, 5, 0));
    assert(!open_on(ctx, 1, 5, 0));
    // The legacy CBC/unchecked paths are not tracked.
    assert(open_on(ctx, 0, 1, CIPHER_FLAG_NO_REPLAY_CHECK));
    assert(open_on(ctx, 0, 1, CIPHER_FLAG_NO_REPLAY_CHECK));
    // A sequence number wrapping past 2^32 still counts as newer.
    PPLT_CRYPTO_CONTEXT wrap = PltCreateCryptoContext(1);
    assert(open_seq(wrap, 0xFFFFFFF0u));
    assert(open_seq(wrap, 3));
    assert(!open_seq(wrap, 0xFFFFFFF0u));
    PltDestroyCryptoContext(wrap);

    PltDestroyCryptoContext(ctx);
    printf("replay window: ok\n");
    return 0;
}
