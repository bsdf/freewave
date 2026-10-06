#include "backend.hh"

auto
reresolve_album_hashes(const Backend &backend, QList<song> &songs) -> bool
{
  bool moved = false;
  for (auto &s : songs)
    {
      auto hash = backend.resolve_album_hash(s.native_album_id);
      if (hash.isEmpty() || hash == s.album_hash)
        continue;
      s.album_hash = hash;
      moved = true;
    }
  return moved;
}
