// SPDX-License-Identifier: GPL-3.0
// Copyright © 2025 Pine (https://github.com/Ishan09811/pine)

#include "compressed_backing.h"
#include <lz4.h>

namespace skyline::vfs {
    constexpr size_t HEADER_FIXED_SIZE = 0x20;

    CompressedBacking::CompressedBacking(std::shared_ptr<Backing> raw) : Backing(mode = {true, false, false}), compressedBacking(std::move(raw)) {
        HeaderOnDisk od = compressedBacking->Read<HeaderOnDisk>(0);

        hdr.magic = od.magic;
        hdr.blockSize = od.blockSize;
        hdr.uncompressedSize = od.uncompressedSize;
        hdr.compressedSize = od.compressedSize;
        hdr.blockCount = od.blockCount;

        u8 rawMagic[4];
        compressedBacking->Read(span<u8>(rawMagic, 4), 0);
      
        if (hdr.magic != util::MakeMagic<u32>("LZ4B")) {
            char hex[32];
            snprintf(hex, sizeof(hex), "%02X %02X %02X %02X", rawMagic[0], rawMagic[1], rawMagic[2], rawMagic[3]);
            throw exception("CompressedBacking: Unsupported format [{}], expected 'LZ4B'", hex);
        }

        if (hdr.blockCount == 0 || hdr.blockCount > 1'000'000)
            throw exception("CompressedBacking: invalid blockCount: {}", hdr.blockCount);
        if (hdr.blockSize == 0 || hdr.blockSize > (1 << 24))
            throw exception("CompressedBacking: invalid blockSize: {}", hdr.blockSize);

        std::vector<u32> tempOffsets(hdr.blockCount + 1);
        compressedBacking->Read(span<u32>(tempOffsets), HEADER_FIXED_SIZE);
        hdr.blockOffsets.resize(hdr.blockCount + 1);
        for (size_t i = 0; i < hdr.blockCount + 1; i++) {
            hdr.blockOffsets[i] = static_cast<u64>(tempOffsets[i]);
        }

        size = static_cast<size_t>(hdr.uncompressedSize);
    }

    size_t CompressedBacking::ReadImpl(span<u8> output, size_t offset) {
        if (offset >= hdr.uncompressedSize)
            return 0;

        size_t end = std::min(offset + output.size(), (size_t)hdr.uncompressedSize);
        size_t remaining = end - offset;

        size_t blockSize = hdr.blockSize;
        size_t blockIndex = offset / blockSize;
        size_t blockOffsetInside = offset % blockSize;

        size_t written = 0;

        std::vector<u8> blockBuffer(blockSize);

        while (remaining > 0 && blockIndex < hdr.blockCount) {
            // Load compressed block
            u64 compStart = hdr.blockOffsets[blockIndex];
            u64 compEnd = hdr.blockOffsets[blockIndex + 1];
            
            if (compEnd < compStart)
                throw exception("LZ4B invalid block offset table");
            
            size_t compSize = compEnd - compStart;

            std::vector<u8> compData(compSize);
            compressedBacking->Read(span<u8>(compData), compStart);

            // Decompress
            int decSize = LZ4_decompress_safe(
                (char*)compData.data(),
                (char*)blockBuffer.data(),
                compSize,
                blockSize
            );

            if (decSize < 0)
                throw exception("LZ4 block decompression failed");

            // Copy the part we need
            size_t copyBegin = blockOffsetInside;
            
            if (decSize <= copyBegin)
                return written;

            size_t copyLen = std::min(remaining, decSize - copyBegin);

            memcpy(output.data() + written, blockBuffer.data() + copyBegin, copyLen);

            written   += copyLen;
            remaining -= copyLen;
 
            blockIndex++;
            blockOffsetInside = 0;
        }

        return written;
    }
}
