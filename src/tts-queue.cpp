#include "tts-queue.hpp"

#include <algorithm>

namespace {
constexpr int kMinLimit = 1;
constexpr int kMaxLimit = 100;
} // namespace

void TtsQueue::setLimit(int limit)
{
    const size_t clamped = size_t(std::clamp(limit, kMinLimit, kMaxLimit));

    std::lock_guard<std::mutex> lock(mutex_);
    limit_ = clamped;

    const auto lowerPriority = [](const Item &lhs, const Item &rhs) {
        if (lhs.priority != rhs.priority)
            return lhs.priority < rhs.priority;
        return lhs.seq < rhs.seq;
    };

    while (items_.size() > limit_) {
        auto victim = std::min_element(items_.begin(), items_.end(), lowerPriority);
        items_.erase(victim);
    }
}

void TtsQueue::push(const ChatMessage &msg, int priority)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!msg.id.isEmpty()) {
        const bool duplicate = std::any_of(items_.begin(), items_.end(),
                                          [&msg](const Item &item) {
                                              return item.msg.id == msg.id;
                                          });
        if (duplicate)
            return;
    }

    while (items_.size() >= limit_) {
        auto victim = std::min_element(items_.begin(), items_.end(),
                                       [](const Item &lhs, const Item &rhs) {
                                           if (lhs.priority != rhs.priority)
                                               return lhs.priority < rhs.priority;
                                           return lhs.seq < rhs.seq;
                                       });
        items_.erase(victim);
    }

    Item item;
    item.msg = msg;
    item.priority = priority;
    item.seq = nextSeq_++;

    /* After every item of equal or higher priority.  nextSeq_ is monotonic,
     * so equal priorities stay FIFO. */
    auto position = std::find_if(items_.begin(), items_.end(),
                                 [priority](const Item &queued) {
                                     return queued.priority < priority;
                                 });
    items_.insert(position, item);

    cv_.notify_one();
}

bool TtsQueue::pop(ChatMessage *out, std::chrono::milliseconds timeout)
{
    if (!out)
        return false;

    std::unique_lock<std::mutex> lock(mutex_);
    const bool ready = cv_.wait_for(lock, timeout, [this] { return !items_.empty(); });
    if (!ready || items_.empty())
        return false;

    *out = items_.front().msg;
    items_.pop_front();
    return true;
}

void TtsQueue::clear()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        items_.clear();
    }
    cv_.notify_all();
}

size_t TtsQueue::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return items_.size();
}

std::vector<ChatMessage> TtsQueue::snapshot(size_t max) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    const size_t count = std::min(max, items_.size());
    std::vector<ChatMessage> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i)
        result.push_back(items_.at(i).msg);
    return result;
}
