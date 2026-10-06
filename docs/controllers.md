# Native controller metadata

Optional subtype bits extend the existing 16-bit arrival capabilities field:

| Definition | Value | Required broad family |
| --- | --- | --- |
| LI_CCAP_DUALSENSE_EDGE | `0x0200` | LI_CTYPE_PS |
| LI_CCAP_XBOX_ELITE | `0x0400` | LI_CTYPE_XBOX |
| LI_CCAP_STEAM_CONTROLLER | `0x0800` | LI_CTYPE_STEAM |
| LI_CCAP_STEAM_DECK | `0x1000` | LI_CTYPE_STEAM |
| LI_CCAP_XBOX_ELITE_SERIES_2 | `0x2000` | LI_CTYPE_XBOX with LI_CCAP_XBOX_ELITE |

`src/Limelight.h` is the source of truth. Before further allocations, inspect
all capability definitions; unknown bits must remain ignorable. Elite without
the Series 2 modifier represents Series 1. Neither supported buttons nor paddle
count establishes subtype. Send only one Valve model bit; missing or contradictory
Valve metadata must not assert a native model on the host.

The packed arrival packet remains 16 bytes; input remains 34 bytes. Capabilities
and both button fields retain little-endian encoding. PADDLE1 through PADDLE4
remain bits 16 through 19. Older hosts can ignore subtype bits and keep their
existing family behavior; older clients continue sending generic model metadata.
This optional fork extension does not require upstream Moonlight clients to change
or add a packet or HID passthrough mechanism.

Controller touch already supports two pads in the same 28-byte packet. Byte 10
is reserved and byte 11 is the pad index (payload offsets 2 and 3). Motion,
battery, rumble and capability negotiation retain their existing formats.
A subtype does not imply that SDL actually exposes touch or feedback functions.

Client policy and runtime limitations are documented in
[Pyrolight](https://github.com/karsyboy/pyrolight/blob/master/docs/NATIVE_CONTROLLERS.md).
Host identities, mapping, lifetime and physical Steam acceptance are documented in
[Pyroshine](https://github.com/karsyboy/pyroshine/blob/main/docs/NATIVE_CONTROLLERS.md).

## Validation

```sh
cmake -S . -B /tmp/controller-protocol-tests -DCONTROLLER_PROTOCOL_TESTS=ON
cmake --build /tmp/controller-protocol-tests
ctest --test-dir /tmp/controller-protocol-tests --output-on-failure
```

Tests check capabilities, old masks, packet sizes, touchpad offset, byte order,
all four extended buttons, simultaneous presses and full release. Run Pyrolight's
client tests and Pyroshine's `scripts/check-controller-protocol.py` against this
header to verify the same assignments on all three sides.
