#include "reorder.hh"

#include <set>
#include <ranges>

namespace reorder {

auto
get_output_order(
    const std::vector<uint32_t> &input, uint32_t target, const std::vector<uint32_t> &indices) -> std::vector<uint32_t>
{
  std::set<uint32_t> idx_set(std::begin(indices), std::end(indices));

  std::vector<uint32_t> out;
  for (const auto &i : input)
    {
      // insert our indices at the requested position
      if (i == target)
        out.insert(std::end(out), std::begin(idx_set), std::end(idx_set));

      // then insert this index unless if we didn't already up there ^
      if (!idx_set.contains(i))
        out.emplace_back(i);
    }
  return out;
}

auto
get_swaps(uint32_t target, const std::vector<uint32_t> &indices)
    -> std::vector<std::pair<uint32_t, uint32_t>>
{
  if (indices.empty()) return {};

  uint32_t max = std::max(target, std::ranges::max(indices));
  auto input_order = std::views::iota(uint32_t{0}, max + 1) | std::ranges::to<std::vector>();

  // get our desired output permutation
  std::vector<uint32_t> output_order = get_output_order(input_order, target, indices);

  spdlog::trace("reorder::get_swaps(...)");
  spdlog::trace("target index  = {}", target);
  spdlog::trace("moved indices = {}", indices);
  spdlog::trace("input  order  = {}", input_order);
  spdlog::trace("output order  = {}", output_order);

  std::vector<std::pair<uint32_t, uint32_t>> swaps;
  std::set<uint32_t> visited;

  for (uint32_t k = 0; k <= max; ++k)
    {
      if (visited.contains(k)) continue;

      // collect this cycle in traversal order
      std::vector<uint32_t> cycle;
      for (uint32_t curr = k; !visited.contains(curr); curr = output_order[curr])
        {
          visited.emplace(curr);
          cycle.push_back(curr);
        }

      // emit swaps pairing the root with each node from the end back to the
      // second — identical post-order effect to the recursive unwind
      for (auto n = cycle.size(); n-- > 1;)
        swaps.emplace_back(cycle[0], cycle[n]);
    }

  spdlog::trace("swaps = {}", swaps);

  return swaps;
}

auto
get_moves(uint32_t target, const std::vector<uint32_t> &indices)
    -> std::vector<std::pair<uint32_t, uint32_t>>
{
  if (indices.empty()) return {};

  uint32_t max = std::max(target, std::ranges::max(indices));
  auto input_order = std::views::iota(uint32_t{0}, max + 1) | std::ranges::to<std::vector>();

  std::vector<uint32_t> output_order = get_output_order(input_order, target, indices);

  // Selection-placement: walk positions left-to-right, and whenever the value
  // at position p isn't the one output_order wants there, move it into place
  // from its current spot (always at index > p, since [0,p) is already fixed).
  // Each step mirrors one `playlistmove FROM TO` on `work`.
  std::vector<uint32_t> work = input_order;
  std::vector<std::pair<uint32_t, uint32_t>> moves;
  for (uint32_t p = 0; p <= max; ++p)
    {
      if (work[p] == output_order[p]) continue;
      uint32_t q = p + 1;
      while (q <= max && work[q] != output_order[p])
        ++q;
      uint32_t val = work[q];
      work.erase(work.begin() + q);
      work.insert(work.begin() + p, val);
      moves.emplace_back(q, p);
    }

  spdlog::trace("moves = {}", moves);

  return moves;
}

}
