// GothicKit dmusic (lib/dmusic), the live music path: Resources::implLoadMusicSegment loads a
// segment by name through a DmLoader whose resolver returns the file bytes from the game assets
// (common/resources.cpp), and GameMusic plays it with DmPerformance_playTransition and renders PCM
// (common/gamemusic.cpp). Here the resolver returns a copy of the fuzz input for every name, so the
// input is the segment and also any style/DLS/band it references: GothicKit's segment, style and DLS
// parsers each skip chunk ids they don't know, so one RIFF file can carry all three (see the seeds).
// Like GameMusic switching themes, a second transition into the segment is then composed while the
// first one plays (Composer.c); its embellishment and timing come from the input's last byte.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dmusic.h"

typedef struct {
  const uint8_t* data;
  size_t         size;
} Input;

static void* resolve(void* ctx, char const* file, size_t* len) {
  (void)file;
  const Input* in = (const Input*)ctx;
  void* bytes = malloc(in->size ? in->size : 1);
  if(bytes == NULL)
    return NULL;
  memcpy(bytes, in->data, in->size);
  *len = in->size;
  return bytes;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  Input in = {data, size};
  DmLoader* loader = NULL;
  if(DmLoader_create(&loader, DmLoader_DOWNLOAD) != DmResult_SUCCESS)
    return 0;
  DmLoader_addResolver(loader, resolve, &in);

  DmSegment* sgt = NULL;
  if(DmLoader_getSegment(loader, "fuzz.sgt", &sgt) == DmResult_SUCCESS && sgt != NULL) {
    DmPerformance* perf = NULL;
    if(DmPerformance_create(&perf, 44100) == DmResult_SUCCESS) {
      if(DmPerformance_playTransition(perf, sgt, DmEmbellishment_NONE, DmTiming_INSTANT) == DmResult_SUCCESS) {
        static int16_t pcm[4096 * 2];
        for(int i = 0; i < 4; ++i)
          DmPerformance_renderPcm(perf, pcm, sizeof(pcm) / sizeof(pcm[0]), (DmRenderOptions)(DmRender_SHORT | DmRender_STEREO));
        const uint8_t ctl = data[size - 1];
        const DmEmbellishmentType emb = (DmEmbellishmentType)(ctl % 7);  // DmEmbellishment_NONE..END_AND_INTRO
        const DmTiming timing = (DmTiming)(1 + (ctl / 7) % 4);             // DmTiming_INSTANT..MEASURE
        if(DmPerformance_playTransition(perf, sgt, emb, timing) == DmResult_SUCCESS) {
          for(int i = 0; i < 4; ++i)
            DmPerformance_renderPcm(perf, pcm, sizeof(pcm) / sizeof(pcm[0]), (DmRenderOptions)(DmRender_SHORT | DmRender_STEREO));
        }
      }
      DmPerformance_release(perf);
    }
    DmSegment_release(sgt);
  }
  DmLoader_release(loader);
  return 0;
}
