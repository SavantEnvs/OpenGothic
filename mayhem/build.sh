#!/usr/bin/env bash
#
# mayhem/build.sh — OpenGothic fuzz harnesses + functional self-test.
#
# The full CMake project needs a Vulkan SDK, X11, glslangValidator, OpenAL and bullet, none of which
# the parsers below touch, so the input parsers are compiled directly from common/ together with the
# few Tempest sources they use (io, log, text codec, exceptions). lib/dmusic (GothicKit, the music
# path the game uses) is built with its own CMake as a static library.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# Every build dir matches upstream's gitignored `build-*/`.
FUZZ_DIR="$SRC/build-fuzz"
TEST_DIR="$SRC/build-test"

OG_SOURCES=(
  common/bink/video.cpp common/bink/frame.cpp
  common/dmusic/riff.cpp common/dmusic/segment.cpp common/dmusic/track.cpp common/dmusic/band.cpp
  common/dmusic/style.cpp common/dmusic/pattern.cpp common/dmusic/dlscollection.cpp
  common/dmusic/wave.cpp common/dmusic/info.cpp common/dmusic/reference.cpp
  common/dmusic/soundfont.cpp common/dmusic/hydra.cpp
  common/utils/inifile.cpp common/utils/fileutil.cpp
  lib/Tempest/Engine/io/idevice.cpp lib/Tempest/Engine/io/odevice.cpp
  lib/Tempest/Engine/io/memreader.cpp lib/Tempest/Engine/io/memwriter.cpp
  lib/Tempest/Engine/io/rfile.cpp lib/Tempest/Engine/io/wfile.cpp
  lib/Tempest/Engine/utility/log.cpp lib/Tempest/Engine/utility/textcodec.cpp
  lib/Tempest/Engine/utility/dir.cpp lib/Tempest/Engine/exceptions/exception.cpp
)
OG_INCLUDES=(-Icommon -Ilib/Tempest/Engine/include -Ilib/Tempest/Engine -Ilib/TinySoundFont)
OG_DEFINES=(-DTSF_NO_STDIO)

# build_og_lib <outdir> <cxxflags...>: compile the parser sources into <outdir>/libog.a
build_og_lib() {
  local out="$1"; shift
  mkdir -p "$out/obj"
  local objs=()
  for s in "${OG_SOURCES[@]}"; do objs+=("$out/obj/$(echo "$s" | tr '/' '_').o"); done
  printf '%s\n' "${OG_SOURCES[@]}" | xargs -P "$MAYHEM_JOBS" -I{} bash -c '
      o="$0/obj/$(echo "{}" | tr "/" "_").o"
      $CXX -std=c++20 '"$*"' '"${OG_INCLUDES[*]} ${OG_DEFINES[*]}"' -c "{}" -o "$o"' "$out"
  rm -f "$out/libog.a"
  ar rcs "$out/libog.a" "${objs[@]}"
}

# build_dmusic <builddir> <cflags>: GothicKit dmusic as a static library (its own ASan switch off;
# the sanitizer flags come in through CMAKE_C_FLAGS).
build_dmusic() {
  cmake -S lib/dmusic -B "$1" -G Ninja -DCMAKE_BUILD_TYPE=None \
        -DDM_BUILD_STATIC=ON -DDM_ENABLE_ASAN=OFF -DDM_BUILD_EXAMPLES=OFF \
        -DCMAKE_C_FLAGS="$2" >/dev/null
  cmake --build "$1" -j"$MAYHEM_JOBS" --target dmusic dmusic-tsf
}

# 1) sanitized, instrumented project code for the fuzzers
FUZZ_CFLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link -O1"
build_og_lib "$FUZZ_DIR" "$FUZZ_CFLAGS"
build_dmusic "$FUZZ_DIR/dmusic" "$FUZZ_CFLAGS"
# TinySoundFont (lib/dmusic/vendor) walks its voice array as `f->voices + f->voiceNum` while it is still
# NULL/0 (tsf_channel_set_pan/_set_volume, called for every band instrument with a pan or volume before
# any note plays). UBSan's pointer-overflow check fires on that NULL+0 for every real band, so nothing
# past the first band update is reachable; only that check is off, only for tsf.c (same sources and
# -fPIC as its CMake target), and this copy of libdmusic-tsf.a is the one the fuzzers link.
mkdir -p "$FUZZ_DIR/dmusic-tsf"
$CC -std=c11 $FUZZ_CFLAGS -fno-sanitize=pointer-overflow -fPIC -Ilib/dmusic/vendor/TinySoundFont \
    -c lib/dmusic/vendor/TinySoundFont/tsf.c -o "$FUZZ_DIR/dmusic-tsf/tsf.o"
rm -f "$FUZZ_DIR/dmusic-tsf/libdmusic-tsf.a"
ar rcs "$FUZZ_DIR/dmusic-tsf/libdmusic-tsf.a" "$FUZZ_DIR/dmusic-tsf/tsf.o"
DMUSIC_LIB="$FUZZ_DIR/dmusic/libdmusic.a $FUZZ_DIR/dmusic-tsf/libdmusic-tsf.a"

# 2) harnesses: libFuzzer binary + run-once standalone reproducer each
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.c" -o "$FUZZ_DIR/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$FUZZ_DIR/standalone_main.o"
for h in fuzz_bink fuzz_dx8_segment fuzz_dx8_style fuzz_dx8_dls fuzz_inifile; do
  $CXX -std=c++20 $FUZZ_CFLAGS "${OG_INCLUDES[@]}" "${OG_DEFINES[@]}" -c "mayhem/harnesses/$h.cpp" -o "$FUZZ_DIR/$h.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$FUZZ_DIR/$h.o" "$FUZZ_DIR/lsan_off.o" \
       "$FUZZ_DIR/libog.a" -lpthread -o "$SRC/$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$FUZZ_DIR/standalone_main.o" "$FUZZ_DIR/$h.o" "$FUZZ_DIR/lsan_off.o" \
       "$FUZZ_DIR/libog.a" -lpthread -o "$SRC/$h-standalone"
done
$CC $FUZZ_CFLAGS -Ilib/dmusic/include -DDM_STATIC=1 -c mayhem/harnesses/fuzz_dmusic_segment.c -o "$FUZZ_DIR/fuzz_dmusic_segment.o"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$FUZZ_DIR/fuzz_dmusic_segment.o" "$FUZZ_DIR/lsan_off.o" \
     $DMUSIC_LIB -lpthread -lm -o "$SRC/fuzz_dmusic_segment"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$FUZZ_DIR/standalone_main.o" "$FUZZ_DIR/fuzz_dmusic_segment.o" "$FUZZ_DIR/lsan_off.o" \
     $DMUSIC_LIB -lpthread -lm -o "$SRC/fuzz_dmusic_segment-standalone"

# 2b) savegame target: Serialize (zip via miniz) + SaveGameHeader + Tempest Pixmap. serialize.cpp also
#     holds the World/Npc (de)serializers; per-function sections + --gc-sections drop them, since the
#     savegame-slot path (GameMenu) never reaches them. Bundled zlib/libpng/squish are dependencies:
#     built uninstrumented (SPEC §6.2 item 19), with DWARF 3.
T=lib/Tempest/Engine
SAV_INCLUDES=(-Icommon -I$T/include -I$T -Ilib/miniz -Ilib/ZenKit/include
              -I$T/thirdparty/libpng/libpng -I$T/thirdparty/libpng -I$T/thirdparty/zlib -I$T/thirdparty/squish)
SAV_DEFINES=(-DMINIZ_NO_STDIO -DSTB_IMAGE_IMPLEMENTATION -DSTB_IMAGE_WRITE_IMPLEMENTATION -DPNG_ARM_NEON_OPT=0)
SAV_DIR="$FUZZ_DIR/savegame"
mkdir -p "$SAV_DIR/dep" "$SAV_DIR/obj"
DEP_FLAGS="-O1 $DEBUG_FLAGS -ffunction-sections -fdata-sections"
for f in $T/thirdparty/zlib/{adler32,crc32,inflate,inftrees,inffast,zutil,deflate,trees,compress,uncompr}.c \
         $T/thirdparty/libpng/libpng/{png,pngerror,pngget,pngmem,pngpread,pngread,pngrio,pngrtran,pngrutil,pngset,pngtrans,pngwio,pngwrite,pngwtran,pngwutil}.c; do
  $CC $DEP_FLAGS -Wno-deprecated-non-prototype "${SAV_INCLUDES[@]}" "${SAV_DEFINES[@]}" -c "$f" -o "$SAV_DIR/dep/$(echo "$f" | tr '/' '_').o"
done
for f in $T/thirdparty/squish/*.cpp; do
  $CXX $DEP_FLAGS "${SAV_INCLUDES[@]}" -c "$f" -o "$SAV_DIR/dep/$(echo "$f" | tr '/' '_').o"
done
SAV_CFLAGS="$FUZZ_CFLAGS -ffunction-sections -fdata-sections"
# miniz reads zip records with deliberate unaligned 32-bit loads (MINIZ_USE_UNALIGNED_LOADS_AND_STORES,
# on for x86); UBSan's alignment check fires on every input, so only that check is off, only for miniz.c.
$CC $SAV_CFLAGS -fno-sanitize=alignment "${SAV_DEFINES[@]}" -Ilib/miniz -c lib/miniz/miniz.c -o "$SAV_DIR/obj/miniz.o"
for f in common/game/serialize.cpp common/game/savegameheader.cpp \
         $T/formats/pixmap.cpp $T/formats/pixmapcodec.cpp $T/formats/image/*.cpp \
         $T/io/idevice.cpp $T/io/odevice.cpp $T/io/memreader.cpp $T/io/memwriter.cpp \
         $T/utility/log.cpp $T/exceptions/exception.cpp mayhem/harnesses/fuzz_savegame.cpp; do
  $CXX -std=c++20 $SAV_CFLAGS "${SAV_INCLUDES[@]}" "${SAV_DEFINES[@]}" -c "$f" -o "$SAV_DIR/obj/$(echo "$f" | tr '/' '_').o"
done
SAV_OBJS=("$SAV_DIR"/obj/*.o "$SAV_DIR"/dep/*.o "$FUZZ_DIR/lsan_off.o")
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE -fuse-ld=lld -Wl,--gc-sections "${SAV_OBJS[@]}" -lpthread -o "$SRC/fuzz_savegame"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -fuse-ld=lld -Wl,--gc-sections "$FUZZ_DIR/standalone_main.o" "${SAV_OBJS[@]}" -lpthread -o "$SRC/fuzz_savegame-standalone"

# 3) the functional self-test, with normal (unsanitized) flags
TEST_CFLAGS="-O1 -g $COVERAGE_FLAGS"
build_og_lib "$TEST_DIR" "$TEST_CFLAGS"
build_dmusic "$TEST_DIR/dmusic" "$TEST_CFLAGS"
TEST_DMUSIC_LIB="$TEST_DIR/dmusic/libdmusic.a $TEST_DIR/dmusic/vendor/libdmusic-tsf.a"
$CXX -std=c++20 $TEST_CFLAGS "${OG_INCLUDES[@]}" "${OG_DEFINES[@]}" -Ilib/dmusic/include -DDM_STATIC=1 \
     mayhem/selftest/selftest.cpp "$TEST_DIR/libog.a" $TEST_DMUSIC_LIB -lpthread -lm -o "$SRC/opengothic_selftest"
