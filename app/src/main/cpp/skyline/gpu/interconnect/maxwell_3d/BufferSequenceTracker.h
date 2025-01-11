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

private:
    size_t currentSequence;
    size_t lastKnownSequence;
};
