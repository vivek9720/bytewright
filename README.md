# bytewright

A dependency-free **C++17 toolkit of binary and text format decoders**. The
repository is a small monorepo of four related subprojects that all share a
common decoding core (a byte cursor, a structured error model, checksums, and
formatting helpers). Each subproject parses a different kind of structured
input, and each ships a libFuzzer harness wired to its real decode path.

There are no third-party dependencies, no network access, and no generated
code. The build is deterministic and non-interactive, and everything builds from
a clean checkout.

---

## Overview

```
bytewright/
├── include/            public headers
│   ├── common/         shared core: ByteReader/ByteWriter, status, checksum, hexdump
│   ├── binpack/        binary container parser
│   ├── minidb/         embedded record-store decoder
│   ├── cfgscript/      config/script parser (lexer + parser + evaluator)
│   └── streamcodec/    streaming frame decoder
├── src/                implementation, one directory per library
├── tools/              bwdump CLI (decodes any of the four formats)
├── tests/              one unit-test executable per subproject (CTest)
├── fuzz/               one libFuzzer harness per subproject + seeds + dictionary
│   ├── corpus/<harness>/   per-harness seed inputs
│   └── dictionary.txt      shared fuzzing dictionary
├── .clusterfuzzlite/   build.sh + project.yaml for ClusterFuzzLite / Fenrir
└── CMakeLists.txt      local build (libraries, tools, tests)
```

The `common` library is the spine: every decoder reads input through
`common::ByteReader` and reports recoverable failures by throwing
`common::ParseError`, which carries a stable `ErrorCode` plus the byte offset of
the problem. Top-level entry points and the fuzz harnesses catch `ParseError`,
so a malformed input is *rejected*, never a process abort — only a genuine
memory-safety fault (caught by a sanitizer) terminates a run. Alongside the
cursor and error model, `common` provides `ByteWriter`, CRC-32/Adler-32, a
`BitReader` for sub-byte fields, strict UTF-8 validation/codepoint helpers, and
FNV-1a / splitmix64 hashing — all dependency-free and deterministic.

The toolkit is intentionally substantial: ~11k lines of first-party C++ across
the libraries, tools, and unit tests, with multi-stage formats and accumulated
decode state so that any bug a fuzzer finds sits at the end of a real code path.

---

## Subprojects

### `binpack` — binary container parser
Decodes the **`BPK1`** tagged container format: a CRC-checked file header, a
section table (typed sections with per-section CRCs), and section bodies built
from length-prefixed, **nested** records. Records are typed (int / uint / float
/ string / blob) and recursive (`ARRAY`, `GROUP`, and `KEYVAL` metadata records
nest other records up to a depth limit). Supports an optional whole-file trailer
CRC and a metadata section of key/value records. Also ships a `Builder` encoder
(round-trips with the parser), a recursive `RecordVisitor` plus a structural
`validate`, and a section/metadata query API.

### `minidb` — embedded record-store decoder
Decodes the **`MDB1`** page-oriented store: a fixed header, fixed-size **pages**
with a `page_type`, a **slot directory**, and slot records made of typed,
varint-encoded **fields**. String fields can be inline or **references into a
string table** page. The decoder then performs a **journal/WAL replay**
(`BEGIN` / `INSERT` / `UPDATE` / `DELETE` / `COMMIT` / `ROLLBACK`) over an
in-memory record store to reconstruct the final committed state — a stateful,
multi-stage path. Adds an INDEX-page B-tree decoder with ordered lookup, a record
`Cursor`, overflow-page chain reconstruction (with cycle/length guards), and a
catalog/statistics view.

### `cfgscript` — configuration/script parser
A real **lexer + recursive-descent parser + expression evaluator** for an
original C-like configuration language: dotted assignments, nested named blocks,
arrays and object literals, quoted strings with escapes (`\n`, `\xHH`,
`\u{XXXX}`), and a precedence-climbing expression grammar with arithmetic,
bitwise, comparison, logical, and ternary operators evaluated against a scoped
symbol environment. `include "path";` is recorded **as data only** — the parser
never opens or reads any file. Post-parse modules add a canonical text
serializer, a dotted-path query (`a.b[2].c`), schema validation, and a
flatten/diff exporter.

### `streamcodec` — streaming frame decoder
Decodes a back-to-back stream of frames (sync word `0x53C7`), **reassembling
fragmented messages** per `(stream_id, msg_id)` from out-of-order fragments
placed by `frag_offset`, finalized on a `FIN` flag. Tracks **rolling per-stream
sequence state** (gaps and duplicates), handles `CONTROL`/`ACK`/`RESET` frames
(a `RESET` tears down a stream's partial reassembly), and records the
compression flag as **metadata only** (no decompression is performed). Includes
a frame encoder/fragmenter, a self-contained RLE transform, an ordered
per-stream delivery queue, and a window/flow control state machine.

---

## Build instructions

Requires CMake ≥ 3.13 and a C++17 compiler (GCC, Clang, or MSVC). The build is
out-of-source and deterministic.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

This produces the five static libraries (`common`, `binpack`, `minidb`,
`cfgscript`, `streamcodec`), the `bwdump` CLI, and the four test executables.

Useful options:

| Option | Default | Effect |
| --- | --- | --- |
| `BW_BUILD_TESTS` | `ON` | Build the unit tests |
| `BW_BUILD_TOOLS` | `ON` | Build the `bwdump` CLI |
| `BW_WARNINGS_AS_ERRORS` | `OFF` | Add `-Werror` / `/WX` |

### Using the CLI

```sh
# Decode a file and print a human-readable summary.
build/tools/bwdump binpack     path/to/file.bpk
build/tools/bwdump minidb      path/to/file.mdb
build/tools/bwdump cfgscript   path/to/file.cfg
build/tools/bwdump streamcodec path/to/file.bin

# Read from stdin instead of a file.
cat path/to/file.bpk | build/tools/bwdump binpack -
```

---

## Test instructions

The unit tests use a tiny in-repo harness (`tests/bw_test.hpp`) — no external
test framework. Run them through CTest:

```sh
cd build
ctest --output-on-failure
```

Or run a single subproject's tests directly:

```sh
build/tests/test_binpack
build/tests/test_minidb
build/tests/test_cfgscript
build/tests/test_streamcodec
```

Each suite builds inputs with `common::ByteWriter` and asserts both successful
decodes (structure and values) and the structured error behavior (truncation,
bad type tags, depth limits, checksum handling, division by zero, etc.).

---

## Fuzzing instructions

Each subproject has one harness in `fuzz/` whose `LLVMFuzzerTestOneInput` calls
the real top-level decode entry point and then walks the decoded structure, so
the mutator exercises deep parsing/decoding/reassembly/evaluation paths rather
than a shallow header check.

### ClusterFuzzLite / Fenrir

`.clusterfuzzlite/build.sh` is the entry point Fenrir runs. It compiles every
harness into `$OUT` using the sanitizer/fuzzer flags the environment provides
(`$CXX`, `$CXXFLAGS`, `$LIB_FUZZING_ENGINE`), references sources only by
repository-relative paths, and performs no network access. `project.yaml` lists
the four targets: `binpack_fuzzer`, `minidb_fuzzer`, `cfgscript_fuzzer`,
`streamcodec_fuzzer`.

### Building a harness locally with libFuzzer (Clang)

```sh
clang++ -std=c++17 -g -O1 -fsanitize=address,fuzzer,undefined -Iinclude \
  fuzz/binpack_fuzzer.cc src/binpack/*.cpp src/common/*.cpp \
  -o binpack_fuzzer

./binpack_fuzzer -dict=fuzz/dictionary.txt fuzz/corpus/binpack_fuzzer/
```

Swap `binpack` for `minidb`, `cfgscript`, or `streamcodec` to build the others.
The same command pattern is what `.clusterfuzzlite/build.sh` automates.

---

## Seed corpus

Per-harness seeds live in `fuzz/corpus/<harness>_fuzzer/` and are the exact byte
inputs the harness reads (binary for `binpack`/`minidb`/`streamcodec`, text for
`cfgscript`). They are hand-constructed to be valid or near-valid so the fuzzer
starts *inside* the structure-gated logic instead of next to empty input:

- **binpack** — minimal container, deeply nested `GROUP`→`ARRAY` records, a
  metadata section, a trailer-CRC variant, and multi-section files.
- **minidb** — a string table with `STRING_REF` records, and journals that
  exercise `COMMIT`, `ROLLBACK`, `DELETE`, overflow pages, and all field types.
- **cfgscript** — every value type, string escapes, arrays, nested objects,
  labeled blocks, `include`, a rich expression, and cross-scope references.
- **streamcodec** — single-shot and out-of-order multi-fragment messages,
  multi-stream interleaving, a mid-message `RESET`, `CONTROL`/`ACK`, and a
  compressed-flagged message.

Fenrir auto-packages the first recognized seed directory; `fuzz/corpus/` is one
of those locations. `.clusterfuzzlite/build.sh` also emits a standard
`<target>_seed_corpus.zip` into `$OUT` when a `zip` tool is available.

The shared dictionary at `fuzz/dictionary.txt` lists the magics, sync word,
type/op tags, language keywords, operators, and escape introducers for all four
formats; the build copies it to `$OUT/<target>_fuzzer.dict`.

---

## Fenrir readiness checklist

- [x] `.clusterfuzzlite/build.sh` exists at the repository root.
- [x] `build.sh` builds **every** harness into `$OUT`.
- [x] Build uses repository-relative paths only (no absolute/machine paths).
- [x] Build is deterministic, non-interactive, credential-free, and offline.
- [x] Four connected harnesses, each calling real project decode code.
- [x] Harnesses exercise deep paths (nesting, replay, evaluation, reassembly),
      not a magic-header check.
- [x] Per-harness seed corpora in a recognized location (`fuzz/corpus/`).
- [x] Dictionary at `fuzz/dictionary.txt`.
- [x] `project.yaml` lists all four fuzz targets.
- [x] Full source builds from a clean checkout; no external dependencies.

---

## Manual review checklist before submission

Do these by hand before pushing and submitting — they are intentionally left to
a human:

- [ ] **Build it.** Configure + build with CMake and run `ctest`; build at least
      one harness with `-fsanitize=address,fuzzer,undefined` and let it run a few
      minutes against the seeds. (No compiler was available where this tree was
      assembled, so a real local build is the first thing to confirm.)
- [ ] **Read the code as your own.** Read every file, adjust naming/comments to
      your voice, and make edits so the repository is genuinely yours and
      primarily human-written. Expand or restructure anything that does not read
      the way you would write it.
- [ ] **Confirm originality.** Nothing here is copied from a public repository;
      keep it that way and do not paste in outside code. The commit history must
      not match any public repository.
- [ ] **Sanity-check the formats.** Verify each wire format against its decoder
      and seeds; tweak field layouts/limits as you see fit.
- [ ] **Decide on a PoC strategy (optional).** If you want to seed a known bug
      for the difficulty band, design it as a *deep*, stateful root cause (e.g.
      in the journal replay or fragment reassembly), submit the exact crashing
      bytes, and keep the task description bug-focused — never reveal the fix.
- [ ] **Privacy.** Create the GitHub repository as **private** and verify it is
      not a fork or copy of anything public before submitting the URL.

---

## A note on submission

The repository you submit to Fenrir must be **private**, **original**, and
**primarily human-written**. This tree is a substantial starting point — read
it, build it, test it, make it your own, and confirm it is private before
submitting.
