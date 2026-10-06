#ifndef REORDER_HH
#define REORDER_HH

#include <vector>
#include <cstdint>

namespace reorder {

std::vector<uint32_t> get_output_order(
    const std::vector<uint32_t> &input, uint32_t target, const std::vector<uint32_t> &indices);

std::vector<std::pair<uint32_t, uint32_t>> get_swaps(
    uint32_t target, const std::vector<uint32_t> &indices);

// Like get_swaps, but for a container that only supports single-element MOVE
// (remove-then-insert) rather than swap — MPD stored playlists
// (`playlistmove FROM TO`) have no swap primitive. Returns (from, to) pairs
// that, applied left-to-right as move(from, to), realize the same permutation
// as get_output_order(0..max, target, indices).
std::vector<std::pair<uint32_t, uint32_t>> get_moves(
    uint32_t target, const std::vector<uint32_t> &indices);

}

#endif /* REORDER_HH */
