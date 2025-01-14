#include <semaphore>

class SemaphoreReleaser {
public:
    explicit SemaphoreReleaser(std::counting_semaphore<6> &sem) : semaphore(sem), released(false) {}
    
    ~SemaphoreReleaser() {
        if (!released) {
            semaphore.release();
        }
    }

    void release() {
        semaphore.release();
        released = true;
    }

private:
    std::counting_semaphore<6> &semaphore;
    bool released;
};
