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
    _Static_assert(PADDLE1_FLAG == 0x10000 && PADDLE2_FLAG == 0x20000 &&
                   PADDLE3_FLAG == 0x40000 && PADDLE4_FLAG == 0x80000, "paddle flags");
    _Static_assert(sizeof(SS_CONTROLLER_ARRIVAL_PACKET) == 16, "arrival wire size");
    _Static_assert(sizeof(SS_CONTROLLER_TOUCH_PACKET) == 28, "touch wire size");
    _Static_assert(offsetof(SS_CONTROLLER_TOUCH_PACKET, touchpadIndex) == 11, "second pad wire offset");
    _Static_assert(sizeof(NV_MULTI_CONTROLLER_PACKET) == 34, "controller wire size");
    _Static_assert(offsetof(NV_MULTI_CONTROLLER_PACKET, buttonFlags2) == 30, "extended flags offset");
    _Static_assert(LI_CCAP_XBOX_ELITE == 0x400, "Elite capability");
    _Static_assert(LI_CCAP_STEAM_CONTROLLER == 0x800, "Steam capability");
    _Static_assert(LI_CCAP_STEAM_DECK == 0x1000, "Deck capability");
    _Static_assert(LI_CCAP_XBOX_ELITE_SERIES_2 == 0x2000, "Elite 2 capability");
    const uint16_t masks[] = {0, 0xff, LI_CCAP_DUAL_TOUCHPAD, LI_CCAP_DUALSENSE_EDGE, 0x3ff,
        LI_CCAP_XBOX_ELITE, LI_CCAP_XBOX_ELITE | LI_CCAP_XBOX_ELITE_SERIES_2,
        LI_CCAP_STEAM_CONTROLLER, LI_CCAP_STEAM_DECK, 0xffff};
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
