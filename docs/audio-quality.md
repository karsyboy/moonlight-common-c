# Audio quality request

A private Pyroshine extension that lets the client choose the Opus bitrate. It
is independent of the video codec and changes no stream layout, so it never
affects decoding.

## Negotiation

| Step | Attribute | Meaning |
| --- | --- | --- |
| RTSP DESCRIBE (host) | `x-moonshine-audio.quality:<n>` | Highest level the host accepts |
| RTSP ANNOUNCE (client) | `x-moonshine-audio.quality:<level>` | Requested level: `0` standard, `1` high, `2` maximum |

`STREAM_CONFIGURATION.audioQuality` takes an `AUDIO_QUALITY_*` value. The
default, `AUDIO_QUALITY_HOST_DEFAULT` (0, as set by
`LiInitializeStreamConfiguration`), sends nothing and keeps upstream behavior.
Otherwise the level is sent only to a Sunshine-family host that advertised the
attribute, reduced to the host's highest level. A missing, empty, duplicated or
non-numeric advertisement means the host does not support it. The host's
level resets on each RTSP handshake (`HostAudioQualityLevel`).

`AUDIO_QUALITY_HIGH` and above also request GameStream high-quality surround
(`x-nv-audio.surround.AudioQuality:1`) from any host at any video bitrate,
instead of only from 15 Mbps, unless the audio renderer reports a slow Opus
decoder. Stereo never uses the high-quality configuration, whose layout the
client does not parse for two channels.

The helpers are in `src/AudioQuality.h`; `src/SdpAttribute.h` holds the strict
SDP attribute lookup shared with the PyroWave negotiation.

## Validation

Configure CMake with `AUDIO_QUALITY_TESTS=ON` and run CTest. The test covers
advertisement parsing, level selection and the generated ANNOUNCE attributes
for H.264, HEVC and AV1, stereo and 5.1, hosts with and without the extension,
and slow decoders.
