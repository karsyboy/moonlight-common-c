// PyroWave extended FEC block layout: a frame packetized by Pyroshine across
// five Reed-Solomon blocks (fixtures/pyrowave-extended-fec.bin, written by
// Pyroshine's `extended_fec_frame_matches_the_client_fixture`) through the
// real RTP queue, Reed-Solomon recovery and depacketizer.
#include "Limelight-internal.h"
#include "PyroWave.h"
#include "RtpVideoQueue.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACKET_SIZE 24
// RTP header and padding, NV header and an 8-byte payload, as Pyroshine sends.
#define SHARD_SIZE (16 + PACKET_SIZE)
#define FRAME_BYTES (849 * 8 - 8)
#define BLOCKS 5

static unsigned char* fixture;
static size_t fixtureShards;
static unsigned delivered;

static unsigned char expectedByte(size_t i) {
    return (unsigned char)(i * 131 + i / 7);
}

static int submit(PDECODE_UNIT du) {
    assert(du->fullLength == FRAME_BYTES);
    size_t offset = 0;
    for (PLENTRY e = du->bufferList; e; e = e->next) {
        for (int i = 0; i < e->length; i++) {
            assert((unsigned char)e->data[i] == expectedByte(offset + i));
        }
        offset += e->length;
    }
    assert(offset == FRAME_BYTES);
    delivered++;
    return DR_OK;
}

static void loadFixture(void) {
    FILE* file = fopen(PYROWAVE_FEC_FIXTURE, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    assert(size > 0 && size % SHARD_SIZE == 0);
    fixture = malloc(size);
    assert(fixture && fread(fixture, 1, size, file) == (size_t)size);
    fclose(file);
    fixtureShards = size / SHARD_SIZE;
}

// Parity shards do not carry the frame's block count in their FEC fields
// alone, so walk the blocks the way the receiver does: data count and FEC
// percentage from each block's first shard.
typedef struct {
    size_t first;
    unsigned data;
    unsigned parity;
} BLOCK;

static unsigned blocksOf(BLOCK* blocks) {
    unsigned count = 0;
    for (size_t index = 0; index < fixtureShards; count++) {
        const NV_VIDEO_PACKET* nv = (const NV_VIDEO_PACKET*)(fixture + index * SHARD_SIZE + 16);
        uint32_t fecInfo = LE32(nv->fecInfo);
        unsigned data = (fecInfo >> 22) & 0x3ff;
        unsigned percentage = (fecInfo >> 4) & 0xff;
        assert(LiFecBlockIndex(nv->multiFecBlocks, true) == count);
        blocks[count] = (BLOCK){index, data, (data * percentage + 99) / 100};
        index += blocks[count].data + blocks[count].parity;
    }
    return count;
}

// Feed every shard of the fixture except those `lose` rejects, in `order`.
static unsigned run(bool extended, bool (*lose)(unsigned block, unsigned index), bool reverseWithinBlock,
                    RTP_VIDEO_STATS* stats) {
    RTP_VIDEO_QUEUE queue;
    BLOCK blocks[16];
    NegotiatedVideoFormat = VIDEO_FORMAT_PYROWAVE;
    NegotiatedPyroWaveExtendedFecBlocks = extended;
    AppVersionQuad[0] = 7; AppVersionQuad[1] = 1; AppVersionQuad[2] = 431; AppVersionQuad[3] = -1;
    StreamConfig.packetSize = PACKET_SIZE;
    VideoCallbacks.capabilities = CAPABILITY_DIRECT_SUBMIT;
    VideoCallbacks.submitDecodeUnit = submit;
    initializeVideoDepacketizer(PACKET_SIZE);
    RtpvInitializeQueue(&queue);
    delivered = 0;

    unsigned blockCount = blocksOf(blocks);
    assert(blockCount == BLOCKS);
    const int receiveSize = PACKET_SIZE + MAX_RTP_HEADER_SIZE;
    for (unsigned b = 0; b < blockCount; b++) {
        unsigned total = blocks[b].data + blocks[b].parity;
        for (unsigned n = 0; n < total; n++) {
            unsigned i = reverseWithinBlock ? total - 1 - n : n;
            if (lose && lose(b, i)) {
                continue;
            }
            char* buffer = calloc(1, receiveSize + sizeof(RTPV_QUEUE_ENTRY));
            assert(buffer);
            memcpy(buffer, fixture + (blocks[b].first + i) * SHARD_SIZE, SHARD_SIZE);
            // VideoStream.c converts the RTP header to host byte order first.
            PRTP_PACKET packet = (PRTP_PACKET)buffer;
            packet->sequenceNumber = BE16(packet->sequenceNumber);
            packet->timestamp = BE32(packet->timestamp);
            packet->ssrc = BE32(packet->ssrc);
            if (RtpvAddPacket(&queue, packet, SHARD_SIZE, (PRTPV_QUEUE_ENTRY)&buffer[receiveSize]) != RTPF_RET_QUEUED) {
                free(buffer);
            }
        }
    }
    *stats = queue.stats;
    RtpvCleanupQueue(&queue);
    destroyVideoDepacketizer();
    return delivered;
}

static bool loseNothing(unsigned block, unsigned index) { (void)block; (void)index; return false; }
// Every 7th shard, data and parity alike: at most 30 of each block's 204,
// within its 34 parity shards.
static bool loseScattered(unsigned block, unsigned index) { return (index + block) % 7 == 0; }
// A burst of exactly the parity count at the start of each block, including
// its first packet (the frame header in block 0).
static bool loseBurst(unsigned block, unsigned index) { (void)block; return index < 34; }
// One more than block 3's parity.
static bool loseTooMuch(unsigned block, unsigned index) { return block == 3 && index < 35; }

int main(void) {
    RTP_VIDEO_STATS stats;
    loadFixture();

    assert(run(true, loseNothing, false, &stats) == 1);
    assert(stats.packetCountFecRecovered == 0);

    assert(run(true, loseScattered, false, &stats) == 1);
    assert(stats.packetCountFecRecovered > 0 && stats.packetCountFecFailed == 0);

    assert(run(true, loseBurst, false, &stats) == 1);
    assert(stats.packetCountFecRecovered == BLOCKS * 34);

    // Reordered arrival within each block.
    assert(run(true, loseScattered, true, &stats) == 1);

    // Loss beyond a block's parity: the frame is never delivered (and never
    // delivered corrupt); the queue counts the data it could not recover.
    assert(run(true, loseTooMuch, false, &stats) == 0);

    // A client that did not negotiate the layout reads only GameStream's two
    // bits per index and would take block 4 for block 0 of a one-block frame:
    // which is why the host sends this layout only to a client that echoed it.
    const NV_VIDEO_PACKET* last = (const NV_VIDEO_PACKET*)(fixture + (fixtureShards - 1) * SHARD_SIZE + 16);
    assert(LiFecBlockIndex(last->multiFecBlocks, true) == 4 && LiFecLastBlockIndex(last->multiFecBlocks, true) == 4);
    assert(LiFecBlockIndex(last->multiFecBlocks, false) == 0 && LiFecLastBlockIndex(last->multiFecBlocks, false) == 0);
    for (unsigned block = 0; block < 16; block++) {
        for (unsigned lastBlock = block; lastBlock < 16; lastBlock++) {
            uint8_t byte = LiFecBlocksByte((uint8_t)block, (uint8_t)lastBlock, true);
            assert(LiFecBlockIndex(byte, true) == block && LiFecLastBlockIndex(byte, true) == lastBlock);
            if (lastBlock < 4) {
                assert(byte == LiFecBlocksByte((uint8_t)block, (uint8_t)lastBlock, false));
            }
        }
    }

    free(fixture);
    printf("pyrowave extended FEC: ok\n");
    return 0;
}
