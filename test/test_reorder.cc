// Correctness tests for reorder::get_output_order and reorder::get_swaps.
//
// Testing strategy
// ----------------
// get_output_order: verify the exact output sequence for a variety of
//   (input, target, indices) combinations.
//
// get_swaps: use a property check — apply the returned swap pairs to the
//   identity vector [0..max] and verify the result matches get_output_order.
//   This is black-box: we don't care which specific swaps are emitted, only
//   that the composed effect is correct.

#include <gtest/gtest.h>
#include <algorithm>
#include <numeric>

#include "reorder.hh"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build the identity vector [0, 1, ..., n-1].
static auto
iota_vec(uint32_t n) -> std::vector<uint32_t>
{
  std::vector<uint32_t> v(n);
  std::iota(v.begin(), v.end(), 0u);
  return v;
}

// Apply a list of (a, b) swap pairs to a copy of v, simulating mpd_send_swap.
static auto
apply_swaps(std::vector<uint32_t> v,
    const std::vector<std::pair<uint32_t, uint32_t>> &swaps)
    -> std::vector<uint32_t>
{
  for (const auto &[a, b] : swaps)
    std::swap(v[a], v[b]);
  return v;
}

// Apply a list of (from, to) move pairs to a copy of v, simulating
// mpd_send_playlist_move (remove at `from`, insert at `to`).
static auto
apply_moves(std::vector<uint32_t> v,
    const std::vector<std::pair<uint32_t, uint32_t>> &moves)
    -> std::vector<uint32_t>
{
  for (const auto &[from, to] : moves)
    {
      uint32_t val = v[from];
      v.erase(v.begin() + from);
      v.insert(v.begin() + to, val);
    }
  return v;
}

// Property: applying get_moves(target, indices) as single-element moves to
// [0..max] must produce the same sequence as get_output_order.
static void
verify_moves(uint32_t target, std::vector<uint32_t> indices)
{
  uint32_t hi = indices.empty()
                    ? target
                    : std::max(target, *std::max_element(indices.begin(), indices.end()));

  auto input = iota_vec(hi + 1);
  auto expected = reorder::get_output_order(input, target, indices);
  auto moves = reorder::get_moves(target, indices);
  auto actual = apply_moves(input, moves);

  EXPECT_EQ(actual, expected);
}

// Property: applying get_swaps(target, indices) to [0..max] must produce the
// same sequence as get_output_order([0..max], target, indices).
static void
verify_swaps(uint32_t target, std::vector<uint32_t> indices)
{
  uint32_t hi = indices.empty()
                    ? target
                    : std::max(target, *std::max_element(indices.begin(), indices.end()));

  auto input = iota_vec(hi + 1);
  auto expected = reorder::get_output_order(input, target, indices);
  auto swaps = reorder::get_swaps(target, indices);
  auto actual = apply_swaps(input, swaps);

  EXPECT_EQ(actual, expected);
}

// ---------------------------------------------------------------------------
// get_output_order
// ---------------------------------------------------------------------------

// Empty indices — input is returned unchanged.
TEST(GetOutputOrder, EmptyIndices_ReturnsInputUnchanged)
{
  auto input = iota_vec(4);
  EXPECT_EQ(reorder::get_output_order(input, 2, {}), input);
}

// Move the last element to the front: [0,1,2,3] → [3,0,1,2].
TEST(GetOutputOrder, MoveSingleElement_ToFront)
{
  EXPECT_EQ(reorder::get_output_order(iota_vec(4), 0, {3}),
      (std::vector<uint32_t>{3, 0, 1, 2}));
}

// Move the first element forward past two others: [0,1,2,3] → [1,2,0,3].
TEST(GetOutputOrder, MoveSingleElement_Forward)
{
  EXPECT_EQ(reorder::get_output_order(iota_vec(4), 3, {0}),
      (std::vector<uint32_t>{1, 2, 0, 3}));
}

// Move a middle element backward: [0,1,2,3] → [0,2,1,3].
TEST(GetOutputOrder, MoveSingleElement_Backward)
{
  EXPECT_EQ(reorder::get_output_order(iota_vec(4), 3, {1}),
      (std::vector<uint32_t>{0, 2, 1, 3}));
}

// Move a two-element block to the front: [0,1,2,3] → [2,3,0,1].
TEST(GetOutputOrder, MoveConsecutiveBlock_ToFront)
{
  EXPECT_EQ(reorder::get_output_order(iota_vec(4), 0, {2, 3}),
      (std::vector<uint32_t>{2, 3, 0, 1}));
}

// Move a two-element block to the back (target = last element):
// [0,1,2,3] → [2,0,1,3].
TEST(GetOutputOrder, MoveConsecutiveBlock_ToBack)
{
  EXPECT_EQ(reorder::get_output_order(iota_vec(4), 3, {0, 1}),
      (std::vector<uint32_t>{2, 0, 1, 3}));
}

// Move non-contiguous elements to a middle position:
// [0,1,2,3,4], target=2, indices={0,4} → [1,0,4,2,3].
TEST(GetOutputOrder, MoveNonContiguousElements_ToMiddle)
{
  EXPECT_EQ(reorder::get_output_order(iota_vec(5), 2, {0, 4}),
      (std::vector<uint32_t>{1, 0, 4, 2, 3}));
}

// Moving an element to its own position is a no-op.
TEST(GetOutputOrder, MoveElementToItsOwnPosition_NoOp)
{
  auto input = iota_vec(4);
  EXPECT_EQ(reorder::get_output_order(input, 1, {1}), input);
}

// Moving all elements to before the first element is a no-op.
TEST(GetOutputOrder, MoveAllElements_ToFront_NoOp)
{
  auto input = iota_vec(4);
  EXPECT_EQ(reorder::get_output_order(input, 0, {0, 1, 2, 3}), input);
}

// Single-element input with a single-element selection is a no-op.
TEST(GetOutputOrder, SingleElement_NoOp)
{
  std::vector<uint32_t> one{0};
  EXPECT_EQ(reorder::get_output_order(one, 0, {0}), one);
  EXPECT_EQ(reorder::get_output_order(one, 0, {}), one);
}

// ---------------------------------------------------------------------------
// get_swaps — property: apply swaps → equals get_output_order result
// ---------------------------------------------------------------------------

// Empty indices → no swaps at all.
TEST(GetSwaps, EmptyIndices_ReturnsNoSwaps)
{
  EXPECT_TRUE(reorder::get_swaps(2, {}).empty());
}

// Moving an element to its own position → no swaps (identity permutation).
TEST(GetSwaps, ElementAlreadyAtTarget_ReturnsNoSwaps)
{
  EXPECT_TRUE(reorder::get_swaps(1, {1}).empty());
}

// All scenarios mirror the get_output_order cases, verified via apply_swaps.

TEST(GetSwaps, MoveSingleElement_ToFront) { verify_swaps(0, {3}); }
TEST(GetSwaps, MoveSingleElement_Forward) { verify_swaps(3, {0}); }
TEST(GetSwaps, MoveSingleElement_Backward) { verify_swaps(3, {1}); }
TEST(GetSwaps, MoveConsecutiveBlock_ToFront) { verify_swaps(0, {2, 3}); }
TEST(GetSwaps, MoveConsecutiveBlock_ToBack) { verify_swaps(3, {0, 1}); }
TEST(GetSwaps, MoveNonContiguousElements) { verify_swaps(2, {0, 4}); }

// Target larger than all indices — input_order is extended to include target.
TEST(GetSwaps, TargetBeyondMaxIndex_RangeExtended)
{
  // target=5, indices={0,1}: input=[0..5], move 0,1 to just before 5.
  verify_swaps(5, {0, 1});
}

// ---------------------------------------------------------------------------
// get_moves — property: apply moves → equals get_output_order result.
// Used for MPD stored playlists (playlistmove: remove-then-insert, no swap).
// ---------------------------------------------------------------------------

// Empty indices → no moves at all.
TEST(GetMoves, EmptyIndices_ReturnsNoMoves)
{
  EXPECT_TRUE(reorder::get_moves(2, {}).empty());
}

// Element already at its target → no moves (identity permutation).
TEST(GetMoves, ElementAlreadyAtTarget_ReturnsNoMoves)
{
  EXPECT_TRUE(reorder::get_moves(1, {1}).empty());
}

TEST(GetMoves, MoveSingleElement_ToFront) { verify_moves(0, {3}); }
TEST(GetMoves, MoveSingleElement_Forward) { verify_moves(3, {0}); }
TEST(GetMoves, MoveSingleElement_Backward) { verify_moves(3, {1}); }
TEST(GetMoves, MoveConsecutiveBlock_ToFront) { verify_moves(0, {2, 3}); }
TEST(GetMoves, MoveConsecutiveBlock_ToBack) { verify_moves(3, {0, 1}); }
TEST(GetMoves, MoveConsecutiveBlock_DownMiddle) { verify_moves(5, {1, 2}); }
TEST(GetMoves, MoveNonContiguousElements) { verify_moves(2, {0, 4}); }
TEST(GetMoves, TargetBeyondMaxIndex_RangeExtended) { verify_moves(5, {0, 1}); }

// Larger scattered case — several elements pulled to the middle.
TEST(GetMoves, LargerScatteredMove) { verify_moves(4, {0, 2, 7}); }

// Only at most (max) moves are ever emitted (one per position that's wrong).
TEST(GetMoves, EmitsAtMostMaxMoves)
{
  auto moves = reorder::get_moves(9, {0, 1, 2});
  EXPECT_LE(moves.size(), 10u);
}

// The result of applying swaps must be a permutation of the original input
// (no elements created or lost).
TEST(GetSwaps, ResultIsPermutationOfInput)
{
  uint32_t target = 2;
  std::vector<uint32_t> indices{0, 4};
  uint32_t hi = std::max(target, *std::max_element(indices.begin(), indices.end()));

  auto input = iota_vec(hi + 1);
  auto swaps = reorder::get_swaps(target, indices);
  auto result = apply_swaps(input, swaps);

  auto sorted = result;
  std::sort(sorted.begin(), sorted.end());
  EXPECT_EQ(sorted, input);
}
