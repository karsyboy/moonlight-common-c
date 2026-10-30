# Client microphone

Apollo's microphone passthrough, as defined by ClassicOldSong's
moonlight-common-c and sent by Nightjar. Pyroshine, Rubylight and Apollo hosts
take it. The microphone is a separate UDP stream from the client to the host;
nothing comes back, and the other streams are unchanged.

## Negotiation

| Step | Message | Meaning |
| --- | --- | --- |
| RTSP DESCRIBE (host) | `m=audio <port> RTP/AVP 96`, then `a=rtpmap:96 opus/48000/1` | The host takes a microphone on `<port>` |
| RTSP DESCRIBE (host) | bit `0x08` in `x-ss-general.encryptionSupported` | The host takes it encrypted |
| RTSP SETUP (client) | `streamid=mic/0/0` (`streamid=mic` before app version 5), after the control stream | `Transport: server_port=` gives the port |
| RTSP ANNOUNCE (client) | bit `0x08` in `x-ss-general.encryptionEnabled` | The client will send it |
| RTSP PLAY (client) | `streamid=mic`, only with per-stream PLAY | |

`STREAM_CONFIGURATION.enableMic` asks for the stream. The microphone is set up
only when the host offers it with encryption; it is never sent in the clear. A
host that refuses SETUP or PLAY costs only the microphone. Without a port in
SETUP, the offered port is used, else 48001.

The stream starts after the input stream, outside the connection stages, and
stops first in `LiStopConnection()`. `LiIsMicrophoneStreamActive()` says
whether it runs; `LiSendMicrophoneOpusData()` sends one packet and is safe to
call from any thread while the connection starts or stops.

## Datagrams

One datagram per mono 48 kHz Opus packet (at most 1356 bytes):

| Bytes | Field |
| --- | --- |
| 0 | flags, 0 |
| 1 | type, `0x61` |
| 2–3 | sequence, little-endian, +1 per packet |
| 4–7 | client milliseconds, little-endian |
| 8–11 | magic `0x12345678`, little-endian |
| 12– | the Opus packet, AES-128-CBC |

The key is the launch's `rikey`; the IV is big-endian `rikeyid + sequence`
followed by 12 zero bytes, as for audio. The packet is padded as the reference
client pads it: to a whole block with bytes holding the pad length, then a
full PKCS#7 block.

## Validation

Configure CMake with `MICROPHONE_TESTS=ON` and run CTest. The test covers the
DESCRIBE offer, the datagram layout and encryption with both padding layers,
size limits and the ANNOUNCE encryption bit.
