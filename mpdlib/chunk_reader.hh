#ifndef CHUNK_READER_HH
#define CHUNK_READER_HH

#include <cstdint>
#include <vector>

namespace mpd {

// Calls reader(offset, buf, chunk_size) repeatedly until it returns <= 0,
// accumulating all data into a single vector.
//
// Reader signature: (std::size_t offset, void* buf, std::size_t size) -> int64_t
template<typename Reader>
auto
collect_chunks(Reader reader, std::size_t chunk_size) -> std::vector<uint8_t>
{
  std::vector<uint8_t> result;
  std::vector<uint8_t> buf(chunk_size);
  int64_t n;
  while ((n = reader(result.size(), buf.data(), buf.size())) > 0)
    result.insert(result.end(), buf.begin(), buf.begin() + n);
  return result;
}

} // namespace mpd

#endif // CHUNK_READER_HH
