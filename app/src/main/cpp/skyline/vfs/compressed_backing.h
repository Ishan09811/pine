// SPDX-License-Identifier: GPL-3.0
// Copyright 2025 Pine (https://github.com/Ishan09811/pine)

#pragma once
#include "backing.h"
#include <lz4.h>
#include <vector>
#include <memory>

namespace skyline::vfs {

enum class CompressionType : u8 {
    None = 0,
    Zeros = 1,
    Two   = 2,
    Lz4   = 3,
    Unknown = 4,
};

struct CompressedEntry {
    s64 virt_offset;
    u64 phys_offset;
    u32 virtual_size;
    u32 physical_size;
    CompressionType type;
};

class CompressedBacking : public Backing {
    std::shared_ptr<Backing> data;
    std::vector<CompressedEntry> entries;

public:
    CompressedBacking(
        std::shared_ptr<Backing> dataBacking,
        std::vector<CompressedEntry> entryList
    )
        : Backing({true,false,false}, 0),
          data(std::move(dataBacking)),
          entries(std::move(entryList))
    {
        std::sort(entries.begin(), entries.end(),
                  [](auto& a, auto& b){ return a.virt_offset < b.virt_offset; });

        if (!entries.empty()) {
            auto& last = entries.back();
            size = last.virt_offset + last.virtual_size;
        }
    }

protected:
    size_t ReadImpl(span<u8> out, size_t offset) override {
        if (offset >= size)
            return 0;

        size_t remaining = out.size();
        size_t written = 0;
        size_t cur = offset;

        size_t idx = FindEntry(cur);
        while (remaining && idx < entries.size()) {
            auto& e = entries[idx];

            if (cur < (size_t)e.virt_offset) {
                size_t gap = std::min(remaining, (size_t)e.virt_offset - cur);
                memset(out.data() + written, 0, gap);
                written += gap;
                remaining -= gap;
                cur += gap;
                continue;
            }

            size_t offInEntry = cur - e.virt_offset;
            if (offInEntry >= e.virtual_size) { idx++; continue; }

            size_t canRead = std::min(remaining, (size_t)e.virtual_size - offInEntry);

            switch (e.type) {
                case CompressionType::None: {
                    size_t phys = e.phys_offset + offInEntry;
                    data->ReadUnchecked(span<u8>(out.data() + written, canRead), phys);
                    break;
                }

                case CompressionType::Zeros: {
                    memset(out.data() + written, 0, canRead);
                    break;
                }

                case CompressionType::Lz4: {
                    std::vector<u8> comp(e.physical_size);
                    data->ReadUnchecked(span<u8>(comp), e.phys_offset);

                    std::vector<u8> decomp(e.virtual_size);
                    int dec = LZ4_decompress_safe(
                        (char*)comp.data(),
                        (char*)decomp.data(),
                        comp.size(),
                        decomp.size()
                    );
                    if (dec < 0 || (u32)dec != e.virtual_size)
                        throw exception("LZ4 decompress failed");

                    memcpy(out.data() + written, decomp.data() + offInEntry, canRead);
                    break;
                }

                default:
                    throw exception("Unsupported compression type");
            }

            written += canRead;
            remaining -= canRead;
            cur += canRead;

            if (offInEntry + canRead >= e.virtual_size)
                idx++;
        }

        if (remaining) {
            memset(out.data() + written, 0, remaining);
            written += remaining;
        }

        return written;
    }

private:
    size_t FindEntry(size_t virt) {
        size_t lo = 0, hi = entries.size();
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            auto& e = entries[mid];
            if (e.virt_offset + e.virtual_size <= virt)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }
};

} // namespace skyline::vfs
