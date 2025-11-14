// SPDX-License-Identifier: GPL-3.0
// Copyright © 2025 Pine (https://github.com/Ishan09811/pine)

#include "compressed_backing.h"
#include <lz4.h>

namespace skyline::vfs {
    CompressedBacking::CompressedBacking(std::shared_ptr<Backing> raw) : Backing(Mode::Read), compressedBacking(std::move(raw)) {
        hdr = compressedBacking->Read<Header>(0);

        if (hdr.magic != util::MakeMagic<u32>("LZ4B"))
            throw exception("Invalid compressed backing");

        hdr.blockOffsets.resize(hdr.blockCount + 1);
        compressedBacking->Read(span<u32>(hdr.blockOffsets), sizeof(Header));

        size = hdr.uncompressedSize;
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
            u32 compStart = hdr.blockOffsets[blockIndex];
            u32 compEnd   = hdr.blockOffsets[blockIndex + 1];
            u32 compSize  = compEnd - compStart;

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
            size_t copyLen   = std::min(remaining, decSize - copyBegin);

            memcpy(output.data() + written, blockBuffer.data() + copyBegin, copyLen);

            written   += copyLen;
            remaining -= copyLen;
 
            blockIndex++;
            blockOffsetInside = 0;
        }

        return written;
    }
}
