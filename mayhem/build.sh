#!/usr/bin/env bash
#
# mayhem/build.sh — build the Socket.IO packet-codec fuzz harness and its known-answer test.
#
# The Unreal plugin itself needs the engine to compile, so the fuzzed surface is the engine-free
# wire-protocol codec: Source/SocketIOLib/Private/internal/sio_packet.cpp (upstream's file,
# unmodified) + sio_message.h + the getnamo/rapidjson fork the plugin vendors as a submodule.
# rapidjson is cached in the image under /opt/toolchains/deps (Dockerfile); the two UE string
# helpers sio_packet.cpp calls are provided by mayhem/shim.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

RAPIDJSON=/opt/toolchains/deps/rapidjson-getnamo-deb1566f/src
[ -f "$RAPIDJSON/include/rapidjson/document.h" ] || { echo "missing cached rapidjson at $RAPIDJSON" >&2; exit 1; }

LIB="$SRC/Source/SocketIOLib"
INC=(-I"$SRC/mayhem/shim" -I"$LIB/Public" -I"$LIB/Private/internal" -I"$RAPIDJSON/include")
STD=(-std=c++17 -Wno-deprecated-declarations)
OBJ="$SRC/mayhem/build"
mkdir -p "$OBJ"

# 1) Sanitized fuzz build. UBSan's `enum` check is the one relaxed check: packet::parse casts the
#    first wire byte straight to packet::frame_type / packet::type and then range-checks the result
#    (`_type < type_min || _type > type_max`), so any payload not starting with '0'..'6' is an
#    out-of-range enum load on the first input and would stop the fuzzer there. ASan and every other
#    UBSan check stay halting. The library TU carries SanCov (-fsanitize=fuzzer-no-link), even when
#    SANITIZER_FLAGS is empty, or Mayhem would record zero edges for the fuzzed code.
FUZZ_CXXFLAGS=($SANITIZER_FLAGS -fno-sanitize=enum $DEBUG_FLAGS -fsanitize=fuzzer-no-link -O1 "${STD[@]}" "${INC[@]}")
$CXX "${FUZZ_CXXFLAGS[@]}" -c "$LIB/Private/internal/sio_packet.cpp" -o "$OBJ/sio_packet.o"
$CXX "${FUZZ_CXXFLAGS[@]}" -c "$SRC/mayhem/fuzz_packet.cc" -o "$OBJ/fuzz_packet.o"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$OBJ/lsan_off.o"
$CC  $SANITIZER_FLAGS $DEBUG_FLAGS -x c -c "$STANDALONE_FUZZ_MAIN" -o "$OBJ/standalone_main.o"

$CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE \
    "$OBJ/fuzz_packet.o" "$OBJ/sio_packet.o" "$OBJ/lsan_off.o" -o /mayhem/fuzz_packet
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS \
    "$OBJ/fuzz_packet.o" "$OBJ/sio_packet.o" "$OBJ/lsan_off.o" "$OBJ/standalone_main.o" \
    -o /mayhem/fuzz_packet-standalone

# 2) Known-answer test with the project's NORMAL flags (no sanitizers, no -gdwarf-3).
$CXX -O2 $COVERAGE_FLAGS "${STD[@]}" "${INC[@]}" \
    "$LIB/Private/internal/sio_packet.cpp" "$SRC/mayhem/selftest_packet.cc" -o /mayhem/selftest_packet
