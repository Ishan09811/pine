#include <functional>
#include <gpu/buffer_manager.h>

class BufferSequenceTracker {
public:
    BufferSequenceTracker() : currentSequence(0), lastKnownSequence(0) {}

    bool IsValid() const {
        return currentSequence > 0;
    }

    bool HasChanged() const {
        return currentSequence != lastKnownSequence;
    }

    void Update() {
        lastKnownSequence = currentSequence;
    }

    void SetSequence(size_t sequence) {
        currentSequence = sequence;
    }

    size_t GenerateBufferBindingHash(const skyline::gpu::BufferBinding& binding) {
        size_t bufferHash = std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(static_cast<VkBuffer>(binding.buffer)));
        size_t offsetHash = std::hash<vk::DeviceSize>{}(binding.offset);
        return bufferHash ^ (offsetHash << 1);
    }

private:
    size_t currentSequence;
    size_t lastKnownSequence;
};
