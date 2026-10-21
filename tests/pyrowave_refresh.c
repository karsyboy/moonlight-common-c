// Test the public refresh entry point against its real control-event owner.
// Including the owner keeps its private event out of the production API.
#include "../src/ControlStream.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>

static bool refresh_pending(void) {
#ifdef LC_WINDOWS
    return WaitForSingleObject(idrFrameRequiredEvent, 0) == WAIT_OBJECT_0;
#else
    PltLockMutex(&idrFrameRequiredEvent.mutex);
    bool pending = idrFrameRequiredEvent.signalled;
    PltUnlockMutex(&idrFrameRequiredEvent.mutex);
    return pending;
#endif
}

int main(void) {
    const int formats[] = {VIDEO_FORMAT_H264, VIDEO_FORMAT_H265, VIDEO_FORMAT_AV1_MAIN8,
        VIDEO_FORMAT_PYROWAVE, VIDEO_FORMAT_PYROWAVE_444,
        VIDEO_FORMAT_PYROWAVE | VIDEO_FORMAT_PYROWAVE_HDR,
        VIDEO_FORMAT_PYROWAVE_444 | VIDEO_FORMAT_PYROWAVE_HDR};
    AppVersionQuad[0] = 7;
    AppVersionQuad[1] = 1;
    AppVersionQuad[2] = 450;
    assert(initializePlatform() == 0);
    for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        NegotiatedVideoFormat = formats[i];
        assert(initializeControlStream() == 0);
        assert(!refresh_pending());
        // Renderer creation/reset must be able to replace a frame discarded
        // while the decoder lock was held, even on an unchanged desktop.
        LiRequestIdrFrame();
        assert(refresh_pending());
        PltClearEvent(&idrFrameRequiredEvent);
        if (formats[i] & VIDEO_FORMAT_MASK_PYROWAVE) {
            // Independent-frame loss still needs no reference invalidation.
            connectionDetectedFrameLoss(1, 2);
            assert(!refresh_pending());
        }
        stopping = true;
        destroyControlStream();
    }
    cleanupPlatform();
    puts("Explicit renderer refresh works for every codec; PyroWave loss remains independent");
    return 0;
}
