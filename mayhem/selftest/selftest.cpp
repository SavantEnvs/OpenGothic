// Known-answer tests for the parsers the fuzz targets cover (upstream ships no test suite).
// Prints one "PASS <name>" / "FAIL <name>" line per check and a final "SUMMARY passed=<p> failed=<f>".
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include <Tempest/File>
#include "bink/video.h"
#include "dmusic/riff.h"
#include "dmusic/segment.h"
#include "utils/inifile.h"

extern "C" {
#include "dmusic.h"
}

static int passed = 0, failed = 0;
static void check(bool ok, const char* name) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
  (ok ? passed : failed)++;
  }

static void put32(std::vector<uint8_t>& v, uint32_t x) {
  for(int i=0;i<4;++i) v.push_back(uint8_t(x>>(8*i)));
  }
static void putId(std::vector<uint8_t>& v, const char* id) {
  v.insert(v.end(), id, id+4);
  }

// ---- Bink ----
class BufInput : public Bink::Video::Input {
  public:
    explicit BufInput(const std::vector<uint8_t>& d):d(d){}
    void read(void* dst, size_t n) override { if(n>d.size()-at) throw std::runtime_error("eof"); std::memcpy(dst,d.data()+at,n); at+=n; }
    void seek(size_t p) override { if(p>d.size()) throw std::runtime_error("eof"); at=p; }
    void skip(size_t n) override { if(n>d.size()-at) throw std::runtime_error("eof"); at+=n; }
    const std::vector<uint8_t>& d; size_t at=0;
  };

static std::vector<uint8_t> binkHeader(uint32_t tag, uint32_t fpsNum, uint32_t fpsDen) {
  std::vector<uint8_t> v;
  put32(v, tag);       // "BIKi"
  put32(v, 64);        // file size - 8
  put32(v, 0);         // frame count
  put32(v, 0);         // largest frame
  put32(v, 0);         // unknown
  put32(v, 32);        // width
  put32(v, 16);        // height
  put32(v, fpsNum);
  put32(v, fpsDen);
  put32(v, 0);         // flags
  put32(v, 0);         // audio tracks
  put32(v, 0);         // first index entry
  put32(v, 0);         // skipped when there are no frames
  return v;
  }

static void testBink() {
  {
    auto v = binkHeader(1766541634u, 25, 1);
    BufInput in(v);
    bool ok = false;
    try {
      Bink::Video vid(&in);
      ok = vid.frameCount()==0 && vid.fps().num==25 && vid.fps().den==1 && vid.audioCount()==0;
      }
    catch(...) { ok = false; }
    check(ok, "bink_header_fps_and_frames");
  }
  {
    auto v = binkHeader(0x41424344u, 25, 1);
    BufInput in(v);
    std::string msg;
    try { Bink::Video vid(&in); } catch(const std::exception& e) { msg = e.what(); }
    check(msg=="invalid codec", "bink_rejects_bad_tag");
  }
  {
    auto v = binkHeader(1766541634u, 0, 1);
    BufInput in(v);
    std::string msg;
    try { Bink::Video vid(&in); } catch(const std::exception& e) { msg = e.what(); }
    check(msg=="invalid header: invalid fps (0 / 1)", "bink_rejects_zero_fps");
  }
  {
    std::vector<uint8_t> v = {'B','I','K'};
    BufInput in(v);
    std::string msg;
    try { Bink::Video vid(&in); } catch(const std::exception& e) { msg = e.what(); }
    check(msg=="eof", "bink_truncated_header");
  }
}

// ---- Dx8 RIFF ----
static void testRiff() {
  std::vector<uint8_t> v;
  putId(v,"RIFF"); put32(v, 4+8+4+8+2);
  putId(v,"DMSG");
  putId(v,"abcd"); put32(v,4); putId(v,"1234");
  putId(v,"efgh"); put32(v,2); v.push_back('x'); v.push_back('y');

  std::string seen;
  size_t total = 0;
  try {
    Dx8::Riff r(v.data(), v.size());
    char form[4];
    r.read(form,4);
    seen.append(form,4);
    r.read([&](Dx8::Riff& ch){
      seen += ":";
      seen.append(ch.id(),4);
      std::vector<uint8_t> body;
      ch.read(body);
      total += body.size();
      seen.append(body.begin(), body.end());
      });
    }
  catch(...) { seen = "exception"; }
  check(seen=="DMSG:abcd1234:efghxy" && total==6, "riff_chunk_walk");

  bool threw = false;
  try {
    Dx8::Riff r(v.data(), 6);
    }
  catch(const std::exception&) { threw = true; }
  check(threw, "riff_truncated_header_throws");

  std::vector<uint8_t> notRiff = {'L','I','S','T',4,0,0,0,'D','M','S','G'};
  std::string msg;
  try { Dx8::Riff r(notRiff.data(), notRiff.size()); Dx8::Segment s(r); } catch(const std::exception& e) { msg = e.what(); }
  check(msg=="not a riff", "segment_requires_riff");

  std::vector<uint8_t> seg;
  putId(seg,"RIFF"); put32(seg, 4+12);
  putId(seg,"DMSG");
  putId(seg,"LIST"); put32(seg,4); putId(seg,"trkl");
  size_t tracks = 99;
  try { Dx8::Riff r(seg.data(), seg.size()); Dx8::Segment s(r); tracks = s.track.size(); } catch(...) {}
  check(tracks==0, "segment_empty_track_list");
}

// ---- IniFile ----
static void testIni() {
  const char* tmp = std::getenv("TMPDIR");
  std::string path = std::string(tmp && *tmp ? tmp : "/tmp") + "/selftest-XXXXXX";
  int fd = mkstemp(path.data());
  if(fd<0) { check(false,"ini_tmpfile"); return; }
  const char* text = "[GAME]\nsightValue=12\nscale = 1.5\nname=hello world\n\n[video]\nzVidResFullscreenX=1024\n";
  if(write(fd, text, std::strlen(text))!=ssize_t(std::strlen(text))) { close(fd); check(false,"ini_write"); return; }
  close(fd);

  try {
    Tempest::RFile f(path.c_str());
    IniFile ini(f);
    check(ini.has("GAME"), "ini_has_section");
    check(!ini.has("SOUND"), "ini_missing_section");
    check(ini.has("GAME","sightValue"), "ini_has_key");
    check(ini.getI("GAME","sightValue")==12, "ini_getI");
    check(std::fabs(ini.getF("GAME","scale")-1.5f)<1e-6f, "ini_getF");
    check(ini.getS("GAME","name")=="hello world", "ini_getS");
    check(ini.getI("GAME","absent",42)==42, "ini_getI_default");
    check(ini.getI("VIDEO","zVidResFullscreenX")==1024, "ini_case_insensitive_section");
    }
  catch(...) { check(false, "ini_exception"); }
  unlink(path.c_str());
}

// ---- GothicKit dmusic ----
static void* noFile(void*, const char*, size_t*) { return nullptr; }
static void* garbage(void*, const char*, size_t* len) {
  void* p = std::malloc(8);
  std::memcpy(p, "NOTARIFF", 8);
  *len = 8;
  return p;
  }

static void testDmusic() {
  DmLoader* loader = nullptr;
  check(DmLoader_create(&loader, DmLoader_DOWNLOAD)==DmResult_SUCCESS && loader!=nullptr, "dmusic_loader_create");
  DmLoader_addResolver(loader, noFile, nullptr);
  DmSegment* sgt = nullptr;
  check(DmLoader_getSegment(loader, "missing.sgt", &sgt)==DmResult_NOT_FOUND && sgt==nullptr, "dmusic_missing_segment");
  DmLoader_release(loader);

  loader = nullptr;
  DmLoader_create(&loader, DmLoader_DOWNLOAD);
  DmLoader_addResolver(loader, garbage, nullptr);
  sgt = nullptr;
  DmResult rv = DmLoader_getSegment(loader, "bad.sgt", &sgt);
  check(rv==DmResult_FILE_CORRUPT, "dmusic_rejects_non_riff");
  DmLoader_release(loader);

  DmPerformance* perf = nullptr;
  check(DmPerformance_create(&perf, 44100)==DmResult_SUCCESS && perf!=nullptr, "dmusic_performance_create");
  std::vector<int16_t> pcm(2048, 0);
  rv = DmPerformance_renderPcm(perf, pcm.data(), pcm.size(), DmRenderOptions(DmRender_SHORT | DmRender_STEREO));
  check(rv==DmResult_SUCCESS, "dmusic_render_idle");
  rv = DmPerformance_renderPcm(perf, pcm.data(), 3, DmRenderOptions(DmRender_SHORT | DmRender_STEREO));
  check(rv==DmResult_INVALID_ARGUMENT, "dmusic_render_rejects_odd_stereo");
  DmPerformance_release(perf);
}

int main() {
  testBink();
  testRiff();
  testIni();
  testDmusic();
  std::printf("SUMMARY passed=%d failed=%d\n", passed, failed);
  return failed==0 ? 0 : 1;
  }
