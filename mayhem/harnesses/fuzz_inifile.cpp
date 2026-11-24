// Gothic.ini / SystemPack.ini / mod .ini parser (common/utils/inifile.cpp). The game opens these
// with IniFile(path) -> RFile -> implRead; the input goes to a scratch file and is opened the same way.
// The parsed values are then queried as GameSettings does (getI/getF/getS on [GAME]/[VIDEO]/... keys)
// and with section/key names taken from the input itself.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include <Tempest/File>
#include "utils/inifile.h"

static std::string scratchPath() {
  const char* tmp = std::getenv("TMPDIR");
  std::string p   = std::string(tmp!=nullptr && tmp[0]!='\0' ? tmp : "/tmp") + "/ini-XXXXXX";
  int fd = mkstemp(p.data());
  if(fd<0)
    std::abort();
  close(fd);
  return p;
  }

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static const std::string path = scratchPath();
  {
    FILE* f = std::fopen(path.c_str(), "wb");
    if(f==nullptr)
      std::abort();
    if(size>0 && std::fwrite(data, 1, size, f)!=size)
      std::abort();
    std::fclose(f);
  }

  try {
    Tempest::RFile fin(path.c_str());
    IniFile ini(fin);
    static const char* secs[] = {"GAME","VIDEO","SOUND","PERFORMANCE","INTERNAL","KEYS","OPTIONS"};
    static const char* keys[] = {"animatedWindows","zVidResFullscreenX","soundVolume","sightValue",
                                 "gameStartFaded","keyAction","zCloudShadowScale","extendedMenu"};
    for(auto s:secs) {
      (void)ini.has(s);
      for(auto k:keys) {
        (void)ini.has(s,k);
        (void)ini.getI(s,k,7);
        (void)ini.getF(s,k,0.5f);
        (void)ini.getS(s,k);
        }
      }
    // names taken from the input: split it at the first two NULs, else use halves
    std::string_view in(reinterpret_cast<const char*>(data), size);
    std::string_view sec = in.substr(0, in.find('\0'));
    std::string_view key = in.substr(std::min(in.size(), sec.size()+1));
    key = key.substr(0, key.find('\0'));
    (void)ini.has(sec);
    (void)ini.getI(sec,key);
    (void)ini.getF(sec,key);
    (void)ini.getS(sec,key);
    }
  catch(const std::exception&) {
    }
  return 0;
  }
