# PyroWave protocol extension

PyroWave is opt-in. Setup normalizes HTTP profile bits and chooses one typed
dialect. Native version 1 takes precedence; Nonary/Vibepollo record framing
requires its verified `186f0393` bitstream advertisement and RTP mapping.
Unknown, empty, duplicate and contradictory capabilities are rejected. The
selected dialect resets on each RTSP handshake. Conventional codecs retain
their existing negotiation and receive behavior.

Native wire-v1 has one path: opaque contiguous codec bytes, ordinary RTP/NV
packetization, existing multi-block FEC/encryption, complete reassembly and
native decode. Packet payload contents never choose a framing mode. Native
frames cannot be partially delivered, even when record-like metadata is present.

Record mode announces features bit 1 and its explicit dialect/family. NV
extraFlags bit `0x80` marks actual record starts, short-header bytes 6–7 carry
the critical prefix count, and decode entries expose RECORD_START/LOST markers.
Missing data may be synthesized only in this mode. A zero-parity final block
with its actual final data shard received has a 1 ms silence deadline, extended
only by unique arrivals. Missing tails wait for a successor. Partial expiry
requires an intact announced critical prefix; protected blocks await FEC.
Cleanup, successor frames and reconnect clear old deadlines/state.

PyroWave requests 8192 negotiated-size packets of receive capacity:
`8192 * (packetSize + 16)`. Other codecs continue requesting 2048 packets.
Checked sizing rejects overflow. Linux SO_RCVBUF returns doubled accounting;
logs report desired/actual/effective capacity and clamping once at setup.
The recommended Linux rmem_max is 32 MiB; UI/remediation belongs to Qt.

Build applications and common-c together: DECODE_UNIT adds record-only critical
packet metadata. It is zero on native/conventional frames. Local PyroWave format
bits must not be confused with Nonary's local bit assignments. HTTP HDR444
translation depends on the negotiated dialect and terminates at this boundary.

Configure CMake with `PYROWAVE_PROTOCOL_TESTS=ON` and
`CONTROLLER_PROTOCOL_TESTS=ON`, then run CTest. Fixtures cover native preference,
record profiles, invalid/duplicate/revision capabilities, complete opaque frame
preservation, large packet counts, loss, reordering, expiry, wrap and native
isolation. Client tests validate real codec golden frames and record parsing.
