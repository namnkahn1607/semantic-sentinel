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

enum class EvictState : uint8_t { COLD = 0, HOT = 1 };

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
};

uint64_t ControlGenerator(NodeState state, EvictState ref_bit, uint64_t length,
                          uint64_t offset);

#endif  // SENTINEL_ENGINE_NODE_HH
