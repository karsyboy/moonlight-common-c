#ifdef NDEBUG
#undef NDEBUG
#endif
#include "Limelight.h"
#include "Platform.h"
#include "Input.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

int main(void) {
    _Static_assert(sizeof(SS_CONTROLLER_ARRIVAL_PACKET) == 16, "arrival wire size");
    _Static_assert(sizeof(NV_MULTI_CONTROLLER_PACKET) == 34, "controller wire size");
    _Static_assert(offsetof(NV_MULTI_CONTROLLER_PACKET, buttonFlags2) == 30, "extended flags offset");
    const uint16_t masks[] = {0, 0xff, LI_CCAP_DUAL_TOUCHPAD, LI_CCAP_DUALSENSE_EDGE, 0x3ff};
    for (unsigned i = 0; i < sizeof(masks) / sizeof(masks[0]); ++i) {
        SS_CONTROLLER_ARRIVAL_PACKET packet = {0};
        packet.controllerNumber = 15;
        packet.type = LI_CTYPE_PS;
        packet.capabilities = LE16(masks[i]);
        packet.supportedButtonFlags = LE32(0xf0000);
        const unsigned char* bytes = (const unsigned char*)&packet;
        const unsigned char expected[] = {15, LI_CTYPE_PS, masks[i] & 0xff, masks[i] >> 8, 0, 0, 15, 0};
        assert(memcmp(bytes + sizeof(NV_INPUT_HEADER), expected, sizeof(expected)) == 0);
    }
    const uint32_t flags[] = {PADDLE1_FLAG, PADDLE2_FLAG, PADDLE3_FLAG, PADDLE4_FLAG,
                             0x3fffff, A_FLAG | TOUCHPAD_FLAG, 0};
    for (unsigned i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) {
        NV_MULTI_CONTROLLER_PACKET packet = {0};
        packet.buttonFlags = LE16((short)flags[i]);
        packet.buttonFlags2 = LE16((short)(flags[i] >> 16));
        const unsigned char* bytes = (const unsigned char*)&packet;
        uint32_t decoded = bytes[16] | (uint32_t)bytes[17] << 8 |
                           (uint32_t)bytes[30] << 16 | (uint32_t)bytes[31] << 24;
        assert(decoded == flags[i]);
    }
    return 0;
}
