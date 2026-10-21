#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

#include "chat-message.hpp"

/**
 * Bounded, thread-safe priority queue for chat messages awaiting speech.
 *
 * Higher priority pops first; equal priority is FIFO.  When the queue is full
 * the oldest item with the lowest priority is dropped.  Message IDs are
 * unique while queued, so a duplicate push is ignored.  The default limit is
 * 25 items and setLimit() clamps to the supported 1-100 range.
 */
class TtsQueue {
public:
    void setLimit(int limit);
    void push(const ChatMessage &msg, int priority);
    bool pop(ChatMessage *out, std::chrono::milliseconds timeout);
    void clear();
    size_t size() const;
    std::vector<ChatMessage> snapshot(size_t max) const;

    /* Monotonic count of messages dropped by limit/shrink eviction. */
    uint64_t evictionCount() const;

private:
    struct Item {
        ChatMessage msg;
        int priority = 0;
        uint64_t seq = 0;
    };

    /* Lock held by the caller; removes the lowest-priority, then oldest,
     * queued item. */
    void evictLowest();
    static bool lowerPriority(const Item &lhs, const Item &rhs);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Item> items_;
    size_t limit_ = 25;
    uint64_t nextSeq_ = 0;
    uint64_t evicted_ = 0;
};
