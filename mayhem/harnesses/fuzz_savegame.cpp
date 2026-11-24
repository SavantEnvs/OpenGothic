// Savegame (.sav) reader: a zip archive (miniz) read through Serialize, as GameMenu lists the save
// slots (common/ui/gamemenu.cpp): open the file, read the "header" entry into a SaveGameHeader
// (versioned name/world/time/game-time decode), then decode the preview image (PNG/JPG via Tempest
// Pixmap) from whichever preview entry exists.
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <system_error>

#include <Tempest/MemReader>
#include <Tempest/Pixmap>

#include "game/serialize.h"
#include "game/savegameheader.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  SaveGameHeader   hdr;
  Tempest::Pixmap  preview;
  try {
    Tempest::MemReader fin(data, size);
    Serialize reader(fin);
    reader.setEntry("header");
    reader.read(hdr);
    if(reader.setEntry("priview.png"))
      reader.read(preview);
    else if(reader.setEntry("preview.png"))
      reader.read(preview);
    else if(reader.setEntry("preview.jpg"))
      reader.read(preview);
    }
  catch(const std::bad_alloc&) {
    }
  catch(const std::system_error&) {
    }
  catch(const std::runtime_error&) {
    }
  return 0;
  }
