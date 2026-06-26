#!/bin/bash -eu
#
# ClusterFuzzLite / OSS-Fuzz build entry point for the bytewright toolkit.
#
# Fenrir runs this script from a clean checkout with $CXX, $CXXFLAGS and
# $LIB_FUZZING_ENGINE pre-set to the sanitizer/fuzzer toolchain. It builds one
# self-contained libFuzzer binary per subproject into $OUT.
#
# Design notes:
#   * No network access, no credentials, no interactive prompts.
#   * All paths are derived from the repository root, never absolute machine
#     paths, so it works whether invoked from $SRC or any other directory.
#   * Each target is compiled in a single translation step (subproject sources +
#     shared common sources + harness) so there is no intermediate object state
#     to manage and the result is fully self-contained.

# Resolve the repository root from this script's location so the build does not
# depend on the caller's working directory.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

INCLUDE="-I${ROOT}/include"
STD="-std=c++17"

# Shared common sources are linked into every harness.
COMMON_SRCS=$(ls "${ROOT}"/src/common/*.cpp)

build_target() {
  target_name="$1"        # e.g. binpack
  harness="${ROOT}/fuzz/${target_name}_fuzzer.cc"
  subproject_srcs=$(ls "${ROOT}"/src/"${target_name}"/*.cpp)

  echo "[build.sh] compiling ${target_name}_fuzzer"
  # shellcheck disable=SC2086
  "$CXX" $CXXFLAGS $STD $INCLUDE \
    "$harness" \
    $subproject_srcs \
    $COMMON_SRCS \
    $LIB_FUZZING_ENGINE \
    -o "${OUT}/${target_name}_fuzzer"

  # Ship the shared dictionary under the per-target name libFuzzer expects.
  if [ -f "${ROOT}/fuzz/dictionary.txt" ]; then
    cp "${ROOT}/fuzz/dictionary.txt" "${OUT}/${target_name}_fuzzer.dict"
  fi

  # Package this target's seed corpus using the standard OSS-Fuzz convention,
  # when a zip tool is available. Fenrir also auto-detects fuzz/corpus/, so this
  # is belt-and-suspenders rather than required.
  corpus_dir="${ROOT}/fuzz/corpus/${target_name}_fuzzer"
  if [ -d "$corpus_dir" ] && command -v zip >/dev/null 2>&1; then
    (cd "$corpus_dir" && zip -q -r -j \
      "${OUT}/${target_name}_fuzzer_seed_corpus.zip" .) || true
  fi
}

for target in binpack minidb cfgscript streamcodec; do
  build_target "$target"
done

echo "[build.sh] done; artifacts in ${OUT}"
ls -1 "${OUT}" || true
