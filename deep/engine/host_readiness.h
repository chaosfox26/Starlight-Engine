#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace host_readiness {

enum class State : uint8_t { Preparing, Ready, Incomplete, Failed, Cancelled };
enum class ItemState : uint8_t { Pending, Ready, Missing, Failed, Degraded };
enum class Need : uint8_t { Required, Optional };

inline const char* stateName(State s) {
    switch (s) {
    case State::Preparing: return "Preparing";
    case State::Ready: return "Ready";
    case State::Incomplete: return "Incomplete";
    case State::Failed: return "Failed";
    case State::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

inline const char* itemStateName(ItemState s) {
    switch (s) {
    case ItemState::Pending: return "Pending";
    case ItemState::Ready: return "Ready";
    case ItemState::Missing: return "Missing";
    case ItemState::Failed: return "Failed";
    case ItemState::Degraded: return "Degraded";
    }
    return "Unknown";
}

struct Item {
    std::string kind;
    std::string name;
    std::string path;
    ItemState state = ItemState::Pending;
    std::string detail;
    Need need = Need::Required;
};

struct Report {
    State state = State::Preparing;
    uint64_t generation = 0;
    uint32_t itemsTotal = 0, itemsReady = 0, itemsMissing = 0, itemsFailed = 0, itemsDegraded = 0;
    uint32_t tasksDone = 0, tasksTotal = 0;
    bool uploadSubmitted = false, fenceRequested = false, fenceConfirmed = false;
    uint64_t fenceValue = 0;
    double elapsedMs = 0;
    std::string reason;
    std::vector<Item> items;
    std::vector<std::string> warnings;
};

class Readiness {
public:
    static constexpr uint64_t kDeviceLost = ~0ull;

    uint64_t begin() {
        static std::atomic<uint64_t> counter{0};
        generation_ = ++counter;
        state_ = State::Preparing;
        items_.clear();
        extraWarnings_.clear();
        reason_.clear();
        uploadSubmitted_ = fenceRequested_ = fenceConfirmed_ = itemReason_ = false;
        fenceValue_ = 0;
        started_ = std::chrono::steady_clock::now();
        fenceStarted_ = started_;
        return generation_;
    }

    uint64_t generation() const { return generation_; }
    bool current(uint64_t generation) const { return generation != 0 && generation == generation_; }
    State state() const { return state_; }
    bool ready() const { return state_ == State::Ready; }
    bool preparing() const { return state_ == State::Preparing; }
    bool stopped() const { return state_ == State::Incomplete || state_ == State::Failed || state_ == State::Cancelled; }
    const std::string& reason() const { return reason_; }
    const std::vector<Item>& items() const { return items_; }
    bool fenceRequested() const { return fenceRequested_; }
    bool fenceConfirmed() const { return fenceConfirmed_; }
    bool uploadSubmitted() const { return uploadSubmitted_; }

    size_t require(const std::string& kind, const std::string& name, const std::string& path, Need need = Need::Required) {
        Item item;
        item.kind = kind;
        item.name = name;
        item.path = path;
        item.need = need;
        items_.push_back(std::move(item));
        return items_.size() - 1;
    }

    size_t find(const std::string& path) const {
        for (size_t i = 0; i < items_.size(); ++i)
            if (items_[i].path == path) return i;
        return npos;
    }

    static constexpr size_t npos = ~size_t(0);

    void markReady(size_t index) {
        if (index >= items_.size()) return;
        if (items_[index].state == ItemState::Missing || items_[index].state == ItemState::Failed || items_[index].state == ItemState::Degraded) return;
        items_[index].state = ItemState::Ready;
        items_[index].detail.clear();
    }

    void markMissing(size_t index, const std::string& detail) {
        if (index >= items_.size()) return;
        if (items_[index].need == Need::Optional) { markDegraded(index, detail); return; }
        items_[index].state = ItemState::Missing;
        items_[index].detail = detail;
        failBecauseOfItems();
    }

    void markFailed(size_t index, const std::string& detail) {
        if (index >= items_.size()) return;
        if (items_[index].need == Need::Optional) { markDegraded(index, detail); return; }
        items_[index].state = ItemState::Failed;
        items_[index].detail = detail;
        failBecauseOfItems();
    }

    void markDegraded(size_t index, const std::string& detail) {
        if (index >= items_.size()) return;
        if (items_[index].state == ItemState::Missing || items_[index].state == ItemState::Failed) return;
        items_[index].state = ItemState::Degraded;
        items_[index].detail = detail;
    }

    void warn(const std::string& text) {
        for (const std::string& known : extraWarnings_)
            if (known == text) return;
        extraWarnings_.push_back(text);
    }

    std::vector<std::string> warnings() const {
        std::vector<std::string> list;
        for (const Item& item : items_)
            if (item.state == ItemState::Degraded) list.push_back(item.kind + " " + item.name + ": " + item.path + (item.detail.empty() ? std::string() : " - " + item.detail));
        list.insert(list.end(), extraWarnings_.begin(), extraWarnings_.end());
        return list;
    }

    size_t warningCount() const { return warnings().size(); }

    void markUploadSubmitted() { uploadSubmitted_ = true; }

    bool hasBlockingItems() const {
        for (const Item& item : items_)
            if (item.state == ItemState::Missing || item.state == ItemState::Failed) return true;
        return false;
    }

    bool allItemsReady() const {
        for (const Item& item : items_)
            if (item.state != ItemState::Ready && item.state != ItemState::Degraded) return false;
        return true;
    }

    void fail(const std::string& why) {
        if (state_ != State::Preparing) return;
        state_ = State::Failed;
        reason_ = why;
    }

    void cancel(const std::string& why) {
        if (state_ != State::Preparing) return;
        state_ = State::Cancelled;
        reason_ = why;
    }

    void incomplete(const std::string& why) {
        if (state_ != State::Preparing) return;
        state_ = State::Incomplete;
        reason_ = why;
    }

    bool beginFence(uint64_t value) {
        if (state_ != State::Preparing) return false;
        if (fenceRequested_) return true;
        fenceValue_ = value;
        fenceRequested_ = true;
        fenceStarted_ = std::chrono::steady_clock::now();
        return true;
    }

    State pollFence(uint64_t completedValue, double timeoutMs) {
        if (state_ != State::Preparing) return state_;
        if (hasBlockingItems()) {
            failBecauseOfItems();
            return state_;
        }
        if (!fenceRequested_) return state_;
        if (completedValue == kDeviceLost) {
            fail("the graphics device was lost while waiting for the GPU fence");
            return state_;
        }
        if (completedValue >= fenceValue_) {
            fenceConfirmed_ = true;
            if (!uploadSubmitted_) {
                incomplete("GPU fence completed but the upload was never reported as submitted");
            } else if (!allItemsReady()) {
                incomplete("GPU fence completed but required content is still unresolved: " + pendingNames());
            } else {
                state_ = State::Ready;
                reason_.clear();
            }
            return state_;
        }
        if (timeoutMs > 0 && millisecondsSince(fenceStarted_) > timeoutMs)
            incomplete("the GPU fence did not complete within " + std::to_string((long long)timeoutMs) + " ms");
        return state_;
    }

    template <class Device>
    bool requestFence(Device& device) {
        if (state_ != State::Preparing) return false;
        if (fenceRequested_) return true;
        const uint64_t value = ++device.fenceVal;
        const long result = static_cast<long>(device.queue->Signal(device.fence.Get(), value));
        if (result < 0) {
            fail("the GPU fence could not be signalled (HRESULT " + hex(static_cast<uint32_t>(result)) + ")");
            return false;
        }
        return beginFence(value);
    }

    template <class Device>
    State poll(Device& device, double timeoutMs) {
        if (state_ != State::Preparing || !fenceRequested_) return pollFence(0, timeoutMs);
        return pollFence(static_cast<uint64_t>(device.fence->GetCompletedValue()), timeoutMs);
    }

    template <class Device, class Pump>
    State settle(Device& device, double timeoutMs, Pump&& pump) {
        requestFence(device);
        while (state_ == State::Preparing) {
            if (!pump()) {
                cancel("loading was cancelled before the content was ready");
                break;
            }
            poll(device, timeoutMs);
            if (state_ == State::Preparing) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return state_;
    }

    Report report() const {
        Report r;
        r.state = state_;
        r.generation = generation_;
        r.itemsTotal = static_cast<uint32_t>(items_.size());
        for (const Item& item : items_) {
            if (item.state == ItemState::Ready) ++r.itemsReady;
            else if (item.state == ItemState::Missing) ++r.itemsMissing;
            else if (item.state == ItemState::Failed) ++r.itemsFailed;
            else if (item.state == ItemState::Degraded) ++r.itemsDegraded;
        }
        r.tasksTotal = r.itemsTotal + 2;
        r.tasksDone = r.itemsReady + r.itemsDegraded + (uploadSubmitted_ ? 1u : 0u) + (fenceConfirmed_ ? 1u : 0u);
        r.uploadSubmitted = uploadSubmitted_;
        r.fenceRequested = fenceRequested_;
        r.fenceConfirmed = fenceConfirmed_;
        r.fenceValue = fenceValue_;
        r.elapsedMs = millisecondsSince(started_);
        r.reason = reason_;
        r.items = items_;
        r.warnings = warnings();
        return r;
    }

    std::string progressText() const {
        const Report r = report();
        return std::to_string(r.tasksDone) + "/" + std::to_string(r.tasksTotal);
    }

    std::string diagnostics() const {
        std::string text;
        for (const Item& item : items_) {
            if (item.state == ItemState::Ready) continue;
            text += std::string("[") + (item.state == ItemState::Degraded ? "Warning" : itemStateName(item.state)) + "] " + item.kind + " " + item.name + ": " + item.path;
            if (!item.detail.empty()) text += " - " + item.detail;
            text += "\n";
        }
        for (const std::string& warning : extraWarnings_) text += "[Warning] " + warning + "\n";
        if (!reason_.empty()) text += reason_ + "\n";
        return text;
    }

    std::string summary() const {
        const Report r = report();
        std::string text = std::string("readiness gen ") + std::to_string(r.generation) + " " + stateName(r.state)
            + " tasks " + std::to_string(r.tasksDone) + "/" + std::to_string(r.tasksTotal)
            + " items " + std::to_string(r.itemsReady) + "/" + std::to_string(r.itemsTotal) + " ready";
        if (r.itemsMissing) text += ", " + std::to_string(r.itemsMissing) + " missing";
        if (r.itemsFailed) text += ", " + std::to_string(r.itemsFailed) + " failed";
        if (r.warnings.size()) text += ", " + std::to_string(r.warnings.size()) + " warnings";
        text += std::string(", upload ") + (r.uploadSubmitted ? "submitted" : "not submitted") + ", fence "
            + (r.fenceConfirmed ? "confirmed" : (r.fenceRequested ? "pending" : "not requested"))
            + ", " + std::to_string((long long)r.elapsedMs) + " ms";
        if (!r.reason.empty()) text += " (" + r.reason + ")";
        return text;
    }

private:
    static double millisecondsSince(std::chrono::steady_clock::time_point t) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
    }

    static std::string hex(uint32_t value) {
        static const char digits[] = "0123456789abcdef";
        std::string text = "0x";
        for (int shift = 28; shift >= 0; shift -= 4) text.push_back(digits[(value >> shift) & 15u]);
        return text;
    }

    std::string pendingNames() const {
        std::string names;
        for (const Item& item : items_) {
            if (item.state == ItemState::Ready || item.state == ItemState::Degraded) continue;
            if (!names.empty()) names += ", ";
            names += item.kind + " " + item.name;
        }
        return names;
    }

    void failBecauseOfItems() {
        if (state_ == State::Preparing) state_ = State::Failed;
        if (state_ != State::Failed || (!reason_.empty() && !itemReason_)) return;
        uint32_t missing = 0, failed = 0;
        for (const Item& item : items_) {
            if (item.state == ItemState::Missing) ++missing;
            else if (item.state == ItemState::Failed) ++failed;
        }
        itemReason_ = true;
        reason_ = "required content could not be loaded (" + std::to_string(missing) + " missing, " + std::to_string(failed) + " failed)";
    }

    State state_ = State::Preparing;
    uint64_t generation_ = 0;
    std::vector<Item> items_;
    std::vector<std::string> extraWarnings_;
    std::string reason_;
    bool uploadSubmitted_ = false, fenceRequested_ = false, fenceConfirmed_ = false, itemReason_ = false;
    uint64_t fenceValue_ = 0;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point fenceStarted_ = std::chrono::steady_clock::now();
};

}
