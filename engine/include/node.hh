//
// Created by nlnk on Apr 19, 26.
//

#ifndef SENTINEL_ENGINE_NODE_HH
#define SENTINEL_ENGINE_NODE_HH

#include <atomic>

enum class NodeState : uint8_t {
    DEAD = 0,
    PENDING = 1,
    READY = 2,
    MIGRATING = 3
};

enum class EvictState : bool { COLD = false, HOT = true };

struct UnpackedControl {
    NodeState state;
    EvictState ref_bit;
    uint32_t length;
    uint32_t offset;
};

[[nodiscard]] inline uint64_t ControlGenerator(NodeState state, EvictState ref_bit,
                                 const uint64_t length, const uint64_t offset) {
    // 0x1FFFFFFF = 29 bits mask
    // 0xFFFFFFFF = 32 bits mask
    return (static_cast<uint64_t>(state) << 62) |
           (static_cast<uint64_t>(ref_bit) << 61) |
           ((length & 0x1FFFFFFF) << 32) | (offset & 0xFFFFFFFF);
}

// Avoid False Sharing using alignas(64) since cache line size for
// almost all modern x86 AMD and Intel CPUs is 64 bytes.
struct alignas(64) MetaNode {
    std::atomic<uint64_t> created_at;

    // Control Block Bit layout
    // Bits 62-63 (2 bits) : State (0=DEAD, 1=PENDING, 2=READY, 3=MIGRATING)
    // Bit  61    (1 bit)  : Ref_bit (0=Cold, 1=Hot)
    // Bits 32-60 (29 bits): Length
    // Bits 0-31  (32 bits): Offset
    std::atomic<uint64_t> control_block;

    [[nodiscard]] inline UnpackedControl LoadControl(
        const std::memory_order order = std::memory_order_acquire) const {
        const uint64_t raw = control_block.load(order);

        return {static_cast<NodeState>(raw >> 62),
                static_cast<EvictState>((raw >> 61) & 1),
                static_cast<uint32_t>((raw >> 32) & 0x1FFFFFFF),
                static_cast<uint32_t>(raw & 0xFFFFFFFF)};
    }
};

#endif  // SENTINEL_ENGINE_NODE_HH
