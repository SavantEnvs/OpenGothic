// DirectMusic downloadable sounds (.dls) parsing, as DirectMusic::dlsCollection() does it
// (common/dmusic/directmusic.cpp): Riff over the file bytes, then Dx8::DlsCollection, which also
// decodes the embedded waves and converts the collection to a SoundFont (hydra.cpp, TinySoundFont).
// Then, as PatternList does for every band instrument, instantiate a SoundFont per instrument patch
// and play a note on it.
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "dmusic/riff.h"
#include "dmusic/dlscollection.h"
#include "dmusic/soundfont.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::vector<uint8_t> v(data, data+size);
  try {
    Dx8::Riff          r(v.data(), v.size());
    Dx8::DlsCollection dls(r);
    for(auto& ins:dls.instrument) {
      uint32_t patch = ((ins.header.Locale.ulBank & 0x7F00) << 8) |
                       ((ins.header.Locale.ulBank & 0x7F) << 8) |
                       (ins.header.Locale.ulInstrument & 0x7F);
      Dx8::SoundFont sf = dls.toSoundfont(patch);
      auto t = sf.noteOn(60, 100);
      float pcm[2*256] = {};
      sf.mix(pcm, 256);
      if(!(t==nullptr))
        Dx8::SoundFont::noteOff(t);
      }
    }
  catch(const std::exception&) {
    }
  return 0;
  }
