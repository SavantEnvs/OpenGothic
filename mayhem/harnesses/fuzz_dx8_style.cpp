// DirectMusic style (.sty) parsing, as DirectMusic::style() does it
// (common/dmusic/directmusic.cpp): Riff over the file bytes, then Dx8::Style.
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "dmusic/riff.h"
#include "dmusic/style.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::vector<uint8_t> v(data, data+size);
  try {
    Dx8::Riff  r(v.data(), v.size());
    Dx8::Style stl(r);
    // what PatternList does with each pattern: resolve its part references and its length
    for(auto& p:stl.patterns) {
      (void)p.timeLength(120.0);
      for(auto& pref:p.partref)
        (void)stl.findPart(pref.io.guidPartID);
      }
    }
  catch(const std::exception&) {
    }
  return 0;
  }
