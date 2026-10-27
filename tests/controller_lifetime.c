// Lifetime of the batched controller packet across input stream teardown and
// queue overflow. Built with AddressSanitizer where available: a cached
// pointer to a freed holder is reported as a heap-use-after-free.
//
// The test includes InputStream.c to reach its internal state without a
// connection or input send thread, so queued holders stay queued.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "InputStream.c"
#include <assert.h>

static void startSession(void) {
    initializeInputStream();
    initialized = true;
}

static void endSession(void) {
    // A sender that exits on a transport failure leaves input queued;
    // destroyInputStream frees it, as connection cleanup does.
    initialized = false;
    destroyInputStream();
}

static int sendState(short controllerNumber, int buttonFlags, short leftStickX) {
    return sendControllerEventInternal(controllerNumber, 0x3, buttonFlags, 0, 0, leftStickX, 0, 0, 0);
}

int main(void) {
    AppVersionQuad[0] = 7; AppVersionQuad[1] = 1; AppVersionQuad[2] = 450;
    assert(initializePlatform() == 0);

    for (int cycle = 0; cycle < 1000; ++cycle) {
        // A controller update still queued at teardown must not be batched
        // into by the next session.
        startSession();
        assert(sendState(0, 0, 10) == 0);
        assert(sendState(1, A_FLAG, 10) == 0);
        endSession();

        startSession();
        assert(sendState(0, 0, 20) == 0);
        assert(sendState(1, A_FLAG, 20) == 0);

        // Changing buttons ends a batch, so each update takes a new holder
        // until the queue is full. The rejected holder is freed and must not
        // remain the batch target.
        for (int i = 0; i <= MAX_QUEUED_INPUT_PACKETS; ++i) {
            sendState(0, (i & 1) ? A_FLAG : 0, (short)i);
        }
        sendState(0, A_FLAG, 30);
        sendState(1, A_FLAG, 30);
        endSession();
    }

    cleanupPlatform();
    return 0;
}
