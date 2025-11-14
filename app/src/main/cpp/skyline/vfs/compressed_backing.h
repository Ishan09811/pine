// SPDX-License-Identifier: GPL-3.0
// Copyright © 2025 Pine (https://github.com/Ishan09811/pine)

#include "backing.h"

namespace skyline::vfs {
    class CompressedBacking : public Backing {
    public:
        CompressedBacking(std::shared_ptr<Backing> rawCompressedData);

        size_t ReadImpl(span<u8> output, size_t offset) override;
        size_t WriteImpl(span<u8> input, size_t offset) override { 
            throw exception("CompressedBacking is read-only");
        }
        void ResizeImpl(size_t) override { 
            throw exception("CompressedBacking does not support resize");
        }

    private:
        struct Header {
            u32 magic;
            u32 blockSize;
            u64 uncompressedSize;
            u64 compressedSize;
            u32 blockCount;
            std::vector<u32> blockOffsets;
        };

        Header hdr;
        std::shared_ptr<Backing> compressedBacking;

        void ReadBlock(size_t blockIndex, span<u8> out);
    };
}
