#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
namespace starlight::editor {
struct SpatialBox {
    double lo[3]{INFINITY, INFINITY, INFINITY}, hi[3]{-INFINITY, -INFINITY, -INFINITY};
    bool finite() const {
        for (int axis = 0; axis < 3; ++axis)
            if (!(std::isfinite(lo[axis]) && std::isfinite(hi[axis]) && lo[axis] <= hi[axis]))
                return false;
        return true;
    }
    bool contains(const SpatialBox &inner) const {
        for (int axis = 0; axis < 3; ++axis)
            if (inner.lo[axis] < lo[axis] || inner.hi[axis] > hi[axis])
                return false;
        return true;
    }
    void include(const SpatialBox &other) {
        for (int axis = 0; axis < 3; ++axis) {
            lo[axis] = std::min(lo[axis], other.lo[axis]);
            hi[axis] = std::max(hi[axis], other.hi[axis]);
        }
    }
};
class SpatialIndex {
  public:
    struct Counters {
        uint64_t queries = 0, visited = 0, reinserted = 0, rebuilt = 0, returned = 0;
    };
    void clear() {
        nodes_.clear();
        free_ = -1;
        root_ = -1;
        entries_.clear();
        unbounded_.clear();
        live_ = 0;
        churn_ = 0;
    }
    size_t size() const { return live_; }
    bool contains(uint32_t slot) const { return slot < entries_.size() && entries_[slot].live; }
    void set(uint32_t slot, const SpatialBox &tight) {
        if (slot >= entries_.size())
            entries_.resize(size_t(slot) + 1);
        Entry &entry = entries_[slot];
        if (!entry.live) {
            entry.live = true;
            ++live_;
        }
        const bool finite = tight.finite();
        bool moved = false;
        if (finite && entry.leaf >= 0 && !entry.unbounded) {
            entry.tight = tight;
            if (nodes_[entry.leaf].fat.contains(tight))
                return;
            remove_leaf(entry.leaf);
            entry.leaf = -1;
            ++counters_.reinserted;
            moved = true;
        }
        if (entry.leaf >= 0) {
            remove_leaf(entry.leaf);
            entry.leaf = -1;
        }
        entry.tight = tight;
        if (!finite) {
            if (!entry.unbounded) {
                entry.unbounded = true;
                unbounded_.push_back(slot);
            }
            return;
        }
        if (entry.unbounded) {
            entry.unbounded = false;
            unbounded_.erase(std::find(unbounded_.begin(), unbounded_.end(), slot));
        }
        if (deferred_) {
            entry.leaf = -2;
            return;
        }
        entry.leaf = new_leaf(slot, tight);
        insert_leaf(entry.leaf);
        if (moved && ++churn_ > std::max<size_t>(64, live_ / 2))
            rebuild();
    }
    void begin_bulk() { deferred_ = true; }
    void end_bulk() {
        deferred_ = false;
        rebuild();
    }
    void erase(uint32_t slot) {
        if (!contains(slot))
            return;
        Entry &entry = entries_[slot];
        if (entry.leaf >= 0)
            remove_leaf(entry.leaf);
        if (entry.unbounded)
            unbounded_.erase(std::find(unbounded_.begin(), unbounded_.end(), slot));
        entry = Entry{};
        --live_;
    }
    void rebuild() {
        nodes_.clear();
        free_ = -1;
        root_ = -1;
        std::vector<int> leaves;
        leaves.reserve(live_);
        for (size_t slot = 0; slot < entries_.size(); ++slot) {
            Entry &entry = entries_[slot];
            entry.leaf = -1;
            if (entry.live && !entry.unbounded) {
                entry.leaf = new_leaf(uint32_t(slot), entry.tight);
                leaves.push_back(entry.leaf);
            }
        }
        if (!leaves.empty())
            root_ = build(leaves, 0, leaves.size());
        churn_ = 0;
        ++counters_.rebuilt;
    }
    void overlap(const SpatialBox &query, std::vector<uint32_t> &out) const {
        out.clear();
        ++counters_.queries;
        Stack stack;
        if (root_ >= 0)
            stack.push(root_);
        while (!stack.empty()) {
            const Node &node = nodes_[stack.pop()];
            ++counters_.visited;
            bool hit = true;
            for (int axis = 0; axis < 3 && hit; ++axis)
                hit = node.fat.lo[axis] <= query.hi[axis] && node.fat.hi[axis] >= query.lo[axis];
            if (!hit)
                continue;
            if (node.left < 0)
                out.push_back(uint32_t(node.slot));
            else {
                stack.push(node.left);
                stack.push(node.right);
            }
        }
        finish(out);
    }
    void segment(const double origin[3], const double direction[3], double length, double pad, std::vector<uint32_t> &out) const {
        out.clear();
        ++counters_.queries;
        Stack stack;
        if (root_ >= 0)
            stack.push(root_);
        while (!stack.empty()) {
            const Node &node = nodes_[stack.pop()];
            ++counters_.visited;
            if (!reaches(node.fat, origin, direction, length, pad))
                continue;
            if (node.left < 0)
                out.push_back(uint32_t(node.slot));
            else {
                stack.push(node.left);
                stack.push(node.right);
            }
        }
        finish(out);
    }
    Counters counters() const { return counters_; }
    int depth() const { return root_ < 0 ? 0 : depth_of(root_); }

  private:
    struct Node {
        SpatialBox fat;
        int parent = -1, left = -1, right = -1, slot = -1;
    };
    struct Entry {
        bool live = false, unbounded = false;
        int leaf = -1;
        SpatialBox tight;
    };
    struct Stack {
        int fixed[96];
        int count = 0;
        std::vector<int> more;
        void push(int value) {
            if (count < 96)
                fixed[count++] = value;
            else
                more.push_back(value);
        }
        bool empty() const { return count == 0 && more.empty(); }
        int pop() {
            if (!more.empty()) {
                int value = more.back();
                more.pop_back();
                return value;
            }
            return fixed[--count];
        }
    };
    static bool reaches(const SpatialBox &box, const double origin[3], const double direction[3], double length, double pad) {
        double nearest = 0, farthest = length + 1e-9;
        for (int axis = 0; axis < 3; ++axis) {
            const double low = box.lo[axis] - pad, high = box.hi[axis] + pad;
            if (std::abs(direction[axis]) < 1e-12) {
                if (origin[axis] < low || origin[axis] > high)
                    return false;
                continue;
            }
            double t1 = (low - origin[axis]) / direction[axis], t2 = (high - origin[axis]) / direction[axis];
            if (t1 > t2)
                std::swap(t1, t2);
            nearest = std::max(nearest, t1);
            farthest = std::min(farthest, t2);
            if (nearest > farthest)
                return false;
        }
        return true;
    }
    static double area(const SpatialBox &box) {
        const double x = box.hi[0] - box.lo[0], y = box.hi[1] - box.lo[1], z = box.hi[2] - box.lo[2];
        return 2 * (x * y + y * z + z * x);
    }
    static SpatialBox joined(const SpatialBox &a, const SpatialBox &b) {
        SpatialBox result = a;
        result.include(b);
        return result;
    }
    void finish(std::vector<uint32_t> &out) const {
        for (uint32_t slot : unbounded_)
            out.push_back(slot);
        std::sort(out.begin(), out.end());
        counters_.returned += out.size();
    }
    int depth_of(int index) const {
        const Node &node = nodes_[index];
        return node.left < 0 ? 1 : 1 + std::max(depth_of(node.left), depth_of(node.right));
    }
    int alloc() {
        if (free_ >= 0) {
            const int index = free_;
            free_ = nodes_[index].parent;
            nodes_[index] = Node{};
            return index;
        }
        nodes_.emplace_back();
        return int(nodes_.size()) - 1;
    }
    void release(int index) {
        nodes_[index].parent = free_;
        nodes_[index].left = nodes_[index].right = nodes_[index].slot = -1;
        free_ = index;
    }
    int new_leaf(uint32_t slot, const SpatialBox &tight) {
        const int index = alloc();
        nodes_[index].slot = int(slot);
        nodes_[index].fat = tight;
        for (int axis = 0; axis < 3; ++axis) {
            nodes_[index].fat.lo[axis] -= kMargin;
            nodes_[index].fat.hi[axis] += kMargin;
        }
        return index;
    }
    void insert_leaf(int leaf) {
        if (root_ < 0) {
            root_ = leaf;
            nodes_[leaf].parent = -1;
            return;
        }
        const SpatialBox box = nodes_[leaf].fat;
        int index = root_;
        while (nodes_[index].left >= 0) {
            const int left = nodes_[index].left, right = nodes_[index].right;
            const double base = area(nodes_[index].fat);
            const double combined = area(joined(nodes_[index].fat, box));
            const double cost = 2 * combined, inheritance = 2 * (combined - base);
            auto descend = [&](int child) {
                const SpatialBox merged = joined(nodes_[child].fat, box);
                return nodes_[child].left < 0 ? area(merged) + inheritance : area(merged) - area(nodes_[child].fat) + inheritance;
            };
            const double costLeft = descend(left), costRight = descend(right);
            if (cost < costLeft && cost < costRight)
                break;
            index = costLeft < costRight ? left : right;
        }
        const int sibling = index, oldParent = nodes_[sibling].parent, parent = alloc();
        nodes_[parent].parent = oldParent;
        nodes_[parent].fat = joined(box, nodes_[sibling].fat);
        if (oldParent >= 0) {
            if (nodes_[oldParent].left == sibling)
                nodes_[oldParent].left = parent;
            else
                nodes_[oldParent].right = parent;
        } else
            root_ = parent;
        nodes_[parent].left = sibling;
        nodes_[parent].right = leaf;
        nodes_[sibling].parent = parent;
        nodes_[leaf].parent = parent;
        refit(nodes_[leaf].parent);
    }
    void refit(int index) {
        while (index >= 0) {
            nodes_[index].fat = joined(nodes_[nodes_[index].left].fat, nodes_[nodes_[index].right].fat);
            index = nodes_[index].parent;
        }
    }
    void remove_leaf(int leaf) {
        if (leaf == root_) {
            root_ = -1;
            release(leaf);
            return;
        }
        const int parent = nodes_[leaf].parent, grand = nodes_[parent].parent;
        const int sibling = nodes_[parent].left == leaf ? nodes_[parent].right : nodes_[parent].left;
        if (grand >= 0) {
            if (nodes_[grand].left == parent)
                nodes_[grand].left = sibling;
            else
                nodes_[grand].right = sibling;
            nodes_[sibling].parent = grand;
            release(parent);
            release(leaf);
            refit(grand);
        } else {
            root_ = sibling;
            nodes_[sibling].parent = -1;
            release(parent);
            release(leaf);
        }
    }
    int build(std::vector<int> &leaves, size_t first, size_t last) {
        if (last - first == 1) {
            nodes_[leaves[first]].parent = -1;
            return leaves[first];
        }
        double low[3] = {INFINITY, INFINITY, INFINITY}, high[3] = {-INFINITY, -INFINITY, -INFINITY};
        for (size_t k = first; k < last; ++k)
            for (int axis = 0; axis < 3; ++axis) {
                const double center = (nodes_[leaves[k]].fat.lo[axis] + nodes_[leaves[k]].fat.hi[axis]) * .5;
                low[axis] = std::min(low[axis], center);
                high[axis] = std::max(high[axis], center);
            }
        int axis = 0;
        for (int k = 1; k < 3; ++k)
            if (high[k] - low[k] > high[axis] - low[axis])
                axis = k;
        const size_t middle = (first + last) / 2;
        std::nth_element(leaves.begin() + first, leaves.begin() + middle, leaves.begin() + last, [&](int a, int b) {
            return nodes_[a].fat.lo[axis] + nodes_[a].fat.hi[axis] < nodes_[b].fat.lo[axis] + nodes_[b].fat.hi[axis];
        });
        const int left = build(leaves, first, middle), right = build(leaves, middle, last), parent = alloc();
        nodes_[parent].left = left;
        nodes_[parent].right = right;
        nodes_[parent].fat = joined(nodes_[left].fat, nodes_[right].fat);
        nodes_[left].parent = nodes_[right].parent = parent;
        return parent;
    }
    static constexpr double kMargin = .1;
    std::vector<Node> nodes_;
    std::vector<Entry> entries_;
    std::vector<uint32_t> unbounded_;
    int root_ = -1, free_ = -1;
    size_t live_ = 0, churn_ = 0;
    bool deferred_ = false;
    mutable Counters counters_;
};
} // namespace starlight::editor
