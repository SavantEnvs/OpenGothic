// DirectMusic segment (.sgt) parsing, as DirectMusic::load(const char16_t*) does it
// (common/dmusic/directmusic.cpp): Riff over the file bytes, then Dx8::Segment.
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "dmusic/riff.h"
#include "dmusic/segment.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::vector<uint8_t> v(data, data+size);
  try {
    Dx8::Riff    r(v.data(), v.size());
    Dx8::Segment sgt(r);
    }
  catch(const std::exception&) {
    }
  return 0;
  }
