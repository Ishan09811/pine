#include <functional>

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

    size_t GenerateBufferBindingHash(const BufferBinding& binding) {
        std::hash<vk::Buffer> bufferHasher;
        std::hash<vk::DeviceSize> sizeHasher;
    
        size_t bufferHash = bufferHasher(binding.buffer);
        size_t offsetHash = sizeHasher(binding.offset);

        return bufferHash ^ (offsetHash << 1);
    }

private:
    size_t currentSequence;
    size_t lastKnownSequence;
};
