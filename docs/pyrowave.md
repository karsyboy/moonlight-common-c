# PyroWave protocol extension

PyroWave is opt-in. Setup normalizes HTTP profile bits and chooses one typed
dialect. Native version 1 takes precedence; Nonary/Vibepollo record framing
requires its verified `186f0393` bitstream advertisement and RTP mapping.
Unknown, empty, duplicate and contradictory capabilities are rejected. The
selected dialect resets on each RTSP handshake. Conventional codecs retain
their existing negotiation and receive behavior.

Explicit renderer/decoder refresh requests use the normal IDR request message
for PyroWave too: the host replays an independent frame. This is needed when a
frame arrived during renderer creation or reset on a static desktop. Ordinary
PyroWave packet loss still requires no reference-frame invalidation.

Native wire-v1 has one path: opaque contiguous codec bytes, ordinary RTP/NV
packetization, existing multi-block FEC/encryption, complete reassembly and
native decode.

GameStream's NV header has two bits each for the current (bits 4–5) and last
(bits 6–7) FEC block index of `multiFecBlocks`: four blocks of at most 255
shards, so large PyroWave frames lose most of their parity. When DESCRIBE offers
`x-ss-pyrowave.fecBlocks:16`, the client echoes it in ANNOUNCE, for either
dialect, and both sides use the extended layout: bits 0–1 and 2–3 carry the
high two bits of each index, for up to sixteen blocks (`Video.h`). Any other
offered value keeps four blocks; the layout is never inferred from packets.
Frames of up to four blocks are byte-identical in both layouts. The RTP queue,
Reed-Solomon recovery (which rewrites recovered headers with the same
encoding) and the depacketizer read the negotiated layout, which resets on each
RTSP handshake. Recovered and unrecoverable data packets are counted in
`RTP_VIDEO_STATS` (`packetCountFecRecovered`, `packetCountFecFailed`). Packet payload contents never choose a framing mode. Native
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
isolation. `pyrowave-extended-fec` feeds a five-block frame packetized by
Pyroshine (`tests/fixtures/pyrowave-extended-fec.bin`, the same bytes its
packetizer test checks) through the queue, Reed-Solomon recovery and
depacketizer with scattered, burst, reordered and excessive loss. Client tests
validate real codec golden frames and record parsing.
