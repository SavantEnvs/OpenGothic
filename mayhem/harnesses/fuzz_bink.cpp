// Bink video/audio decoder (common/bink): the game feeds it .bik files from Video/ and mods
// (ui/videowidget.cpp). The Input subclass mirrors videowidget's file-backed Input: read/seek/skip
// throw past the end of the data.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "bink/video.h"

namespace {
class BufInput : public Bink::Video::Input {
  public:
    BufInput(const uint8_t* d, size_t s) : data(d), size(s) {}
    void read(void* dest, size_t count) override {
      if(count>size-at)
        throw std::runtime_error("eof");
      std::memcpy(dest, data+at, count);
      at += count;
      }
    void seek(size_t pos) override {
      if(pos>size)
        throw std::runtime_error("eof");
      at = pos;
      }
    void skip(size_t count) override {
      if(count>size-at)
        throw std::runtime_error("eof");
      at += count;
      }
  private:
    const uint8_t* data;
    size_t         size;
    size_t         at = 0;
  };
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  BufInput in(data, size);
  try {
    Bink::Video vid(&in);
    for(size_t i=0; i<vid.frameCount(); ++i) {
      try {
        vid.nextFrame();
        }
      catch(const Bink::VideoDecodingException&) {
        // videowidget.cpp treats a video decoding error as recoverable and keeps playing
        }
      }
    }
  catch(const std::exception&) {
    }
  return 0;
  }
