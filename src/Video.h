#pragma once

#include "LinkedBlockingQueue.h"

typedef struct _QUEUED_DECODE_UNIT {
    DECODE_UNIT decodeUnit;
    LINKED_BLOCKING_QUEUE_ENTRY entry;
} QUEUED_DECODE_UNIT, *PQUEUED_DECODE_UNIT;

#pragma pack(push, 1)

// The encrypted video header must be a multiple
// of 16 bytes in size to ensure the block size
// for FEC stays a multiple of 16 too.
typedef struct _ENC_VIDEO_HEADER {
    uint8_t iv[12];
    uint32_t frameNumber;
    uint8_t tag[16];
} ENC_VIDEO_HEADER, *PENC_VIDEO_HEADER;

#define FLAG_CONTAINS_PIC_DATA 0x1
#define FLAG_EOF 0x2
#define FLAG_SOF 0x4

#define NV_VIDEO_PACKET_EXTRA_FLAG_LTR_FRAME 0x1

typedef struct _NV_VIDEO_PACKET {
    uint32_t streamPacketIndex;
    uint32_t frameIndex;
    uint8_t flags;
    uint8_t extraFlags;
    uint8_t multiFecFlags;
    uint8_t multiFecBlocks;
    uint32_t fecInfo;
} NV_VIDEO_PACKET, *PNV_VIDEO_PACKET;

// NV_VIDEO_PACKET::multiFecBlocks carries the current FEC block index in bits
// 4-5 and the last block index in bits 6-7, so GameStream frames span at most
// four blocks. With the PyroWave extended layout (x-ss-pyrowave.fecBlocks:16,
// offered by the host and echoed by us) bits 0-1 and 2-3 carry the high bits
// of each index, for up to sixteen blocks. GameStream hosts leave them zero.
#define PYROWAVE_EXTENDED_FEC_BLOCKS 16
static inline uint8_t LiFecBlockIndex(uint8_t multiFecBlocks, bool extended) {
    return (uint8_t)(((multiFecBlocks >> 4) & 0x3) | (extended ? (multiFecBlocks & 0x3) << 2 : 0));
}
static inline uint8_t LiFecLastBlockIndex(uint8_t multiFecBlocks, bool extended) {
    return (uint8_t)(((multiFecBlocks >> 6) & 0x3) | (extended ? ((multiFecBlocks >> 2) & 0x3) << 2 : 0));
}
static inline uint8_t LiFecBlocksByte(uint8_t block, uint8_t lastBlock, bool extended) {
    uint8_t value = (uint8_t)(((block & 0x3) << 4) | ((lastBlock & 0x3) << 6));
    if (extended) {
        value |= (uint8_t)(((block >> 2) & 0x3) | (((lastBlock >> 2) & 0x3) << 2));
    }
    return value;
}

#define FLAG_EXTENSION 0x10

#define FIXED_RTP_HEADER_SIZE 12
#define MAX_RTP_HEADER_SIZE 16

typedef struct _RTP_PACKET {
    uint8_t header;
    uint8_t packetType;
    uint16_t sequenceNumber;
    uint32_t timestamp;
    uint32_t ssrc;
} RTP_PACKET, *PRTP_PACKET;

// Fields are big-endian
typedef struct _SS_PING {
    char payload[16];
    uint32_t sequenceNumber;
} SS_PING, *PSS_PING;

// Fields are big-endian
#define SS_FRAME_FEC_PTYPE 0x5502
typedef struct _SS_FRAME_FEC_STATUS {
    uint32_t frameIndex;
    uint16_t highestReceivedSequenceNumber;
    uint16_t nextContiguousSequenceNumber;
    uint16_t missingPacketsBeforeHighestReceived;
    uint16_t totalDataPackets;
    uint16_t totalParityPackets;
    uint16_t receivedDataPackets;
    uint16_t receivedParityPackets;
    uint8_t fecPercentage;
    uint8_t multiFecBlockIndex;
    uint8_t multiFecBlockCount;
} SS_FRAME_FEC_STATUS, *PSS_FRAME_FEC_STATUS;

// Fields are little-endian
#define SS_LTR_FRAME_ACK_PTYPE 0x0350
typedef struct _SS_LTR_FRAME_ACK {
    uint32_t frameIndex;
    uint32_t reserved;
} SS_LTR_FRAME_ACK, *PSS_LTR_FRAME_ACK;

// Fields are little-endian
#define SS_RFI_REQUEST_PTYPE 0x0301
typedef struct _SS_RFI_REQUEST {
    uint32_t firstFrameIndex;
    uint32_t reserved1;
    uint32_t lastFrameIndex;
    uint32_t reserved2[3];
} SS_RFI_REQUEST, *PSS_RFI_REQUEST;

#pragma pack(pop)

#define NV_VIDEO_PACKET_EXTRA_FLAG_PYROWAVE_RECORD_START 0x80
