#pragma once

#include <atomic>

/**
 * Guarantees that exactly one active OBS source drains the shared TTS buffer.
 *
 * OBS creates every source in a scene collection, and each source instance
 * starts its own feeder thread.  Without arbitration, duplicate
 * "YouTube Chat TTS" sources (or sources in other scenes) all pop from the
 * same AudioBus, consuming speech several times faster than real time.  The
 * first active owner becomes the consumer; the rest emit silence until it
 * releases.
 *
 * All methods are thread-safe.  The owner pointer is only used as an identity
 * token and is never dereferenced.
 */
class AudioConsumerArbiter {
public:
    /** Try to become the consumer.  Returns true only if the caller owns it
     * after the call (including when it already did). */
    bool tryAcquire(const void *owner)
    {
        if (!owner)
            return false;
        if (consumer_.load(std::memory_order_acquire) == owner)
            return true;

        const void *expected = nullptr;
        return consumer_.compare_exchange_strong(expected, owner, std::memory_order_acq_rel,
                                                 std::memory_order_acquire);
    }

    /** Give up ownership if the caller is the consumer.  A non-owner release
     * is a no-op, so a late deactivate cannot steal another source's turn. */
    void release(const void *owner)
    {
        if (!owner)
            return;
        const void *expected = owner;
        consumer_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel,
                                          std::memory_order_acquire);
    }

    bool isConsumer(const void *owner) const
    {
        return owner != nullptr && consumer_.load(std::memory_order_acquire) == owner;
    }

private:
    std::atomic<const void *> consumer_{nullptr};
};
