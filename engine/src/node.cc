//
// Created by nlnk on Apr 19, 26.
//

#include "node.hh"

uint64_t ControlGenerator(NodeState state, EvictState ref_bit,
                          const uint64_t length, const uint64_t offset) {
    // 0x1FFFFFFF = 29 bits mask
    // 0xFFFFFFFF = 32 bits mask
    return (static_cast<uint64_t>(state) << 62) |
           (static_cast<uint64_t>(ref_bit) << 61) |
           ((length & 0x1FFFFFFF) << 32) | (offset & 0xFFFFFFFF);
}
