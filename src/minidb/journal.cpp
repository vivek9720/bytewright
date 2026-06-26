// bytewright - minidb stage 4: journal / transaction replay.
//
// From header.journal_offset to EOF the file carries a sequence of journal
// entries, each led by a 1-byte op (see JournalOp). The replay maintains two
// layers of state:
//
//   committed_  - the durable logical store, (page,slot) -> record bytes
//   staged_     - ops buffered inside the currently-open transaction
//
// BEGIN opens a transaction (staging starts). INSERT/UPDATE stage a slot's new
// bytes; DELETE stages a tombstone. COMMIT folds the staged ops into committed_
// in order; ROLLBACK discards them. Ops seen outside any transaction apply
// directly to committed_ (auto-commit), which keeps the simplest journals - a
// bare INSERT - meaningful.
//
// This accumulated, mutable cross-op state is the deep path. After the whole
// journal is replayed we decode every surviving committed record's bytes into
// db.records, resolving STRING_REFs against the already-parsed string table.
#include "minidb/decoder.hpp"

#include <map>
#include <utility>
#include <vector>

#include "common/byte_reader.hpp"
#include "common/status.hpp"

namespace bw {
namespace minidb {

namespace {

// A staged mutation. `tombstone` distinguishes a staged DELETE from a staged
// INSERT/UPDATE (whose new bytes live in `bytes`).
struct StagedOp {
  RecordKey key;
  bool tombstone = false;
  std::vector<std::uint8_t> bytes;
};

// Read the common (page u32, slot u16) locator shared by INSERT/UPDATE/DELETE.
RecordKey read_key(common::ByteReader& r) {
  const std::uint32_t page = r.read_u32le();
  const std::uint16_t slot = r.read_u16le();
  return RecordKey{page, slot};
}

// Seed the durable store from the records physically resident in DATA-page
// slots. The journal then mutates this store on top - exactly as a real WAL
// replays edits against the last checkpointed page image. A slot flagged
// out-of-bounds by stage 2 is skipped; an in-bounds slot is copied verbatim from
// the page's available bytes (re-clamped to what actually exists in the buffer).
void seed_from_pages(const std::uint8_t* data, std::size_t size,
                     const std::vector<PageSpan>& spans, const Database& db,
                     std::map<RecordKey, std::vector<std::uint8_t>>& store) {
  for (const Page& page : db.pages) {
    if (page.type != PageType::kData) {
      continue;  // only DATA pages carry decodable records
    }
    if (page.index >= spans.size()) {
      continue;
    }
    const PageSpan& span = spans[page.index];
    for (std::uint16_t s = 0; s < page.slots.size(); ++s) {
      const Slot& slot = page.slots[s];
      if (!slot.in_bounds || slot.rec_length == 0) {
        continue;
      }
      // Absolute span of the record inside the buffer, re-clamped to EOF.
      const std::size_t abs = span.file_offset +
                              static_cast<std::size_t>(slot.rec_offset);
      const std::size_t end = abs + static_cast<std::size_t>(slot.rec_length);
      if (abs >= size || end > size || end < abs) {
        continue;  // physically truncated record: skip rather than over-read
      }
      store[RecordKey{page.index, s}] =
          std::vector<std::uint8_t>(data + abs, data + end);
    }
  }
}

}  // namespace

void replay_journal(const std::uint8_t* data, std::size_t size,
                    const FileHeader& hdr,
                    const std::vector<PageSpan>& spans, Database& db) {
  // The durable store keyed by (page,slot). We keep raw record bytes here and
  // only decode into Field structures once, after replay settles, so that an
  // overwrite/rollback never pays for decoding bytes that get discarded. It is
  // seeded from the page-resident records and then mutated by the journal.
  std::map<RecordKey, std::vector<std::uint8_t>> committed;
  seed_from_pages(data, size, spans, db, committed);

  // journal_offset == 0 means "no journal". An offset at or past EOF is treated
  // as an empty journal (lenient). In either case we still decode the seeded
  // page-resident records below.
  const std::size_t jo = static_cast<std::size_t>(hdr.journal_offset);
  const bool have_journal = (hdr.journal_offset != 0) && (jo < size);
  if (hdr.journal_offset != 0) {
    db.has_journal = true;
  }

  // Transaction state.
  bool in_txn = false;
  std::vector<StagedOp> staged;

  // Apply a settled op directly to the committed store.
  auto apply_committed = [&](const StagedOp& op) {
    if (op.tombstone) {
      committed.erase(op.key);
    } else {
      committed[op.key] = op.bytes;  // INSERT and UPDATE both set the bytes
    }
  };

  // Route an op to either the staging buffer (inside a txn) or straight to the
  // committed store (auto-commit outside a txn).
  auto route = [&](StagedOp&& op) {
    if (in_txn) {
      staged.push_back(std::move(op));
    } else {
      apply_committed(op);
      ++db.committed_ops;
    }
  };

  if (have_journal) {
    common::ByteReader r(data + jo, size - jo);
    bool stop = false;
    while (!stop && !r.eof()) {
      const std::uint8_t op_byte = r.read_u8();
      const JournalOp op = static_cast<JournalOp>(op_byte);

      switch (op) {
        case JournalOp::kBegin:
          // A BEGIN inside an open txn implicitly discards the in-flight one - a
          // crashed writer can leave a dangling BEGIN. Treat the new BEGIN as
          // the authoritative transaction boundary.
          staged.clear();
          in_txn = true;
          break;

        case JournalOp::kInsert:
        case JournalOp::kUpdate: {
          StagedOp s;
          s.key = read_key(r);
          const std::uint64_t rec_len = r.read_varint();
          r.require(static_cast<std::size_t>(rec_len));
          s.bytes = r.read_bytes(static_cast<std::size_t>(rec_len));
          s.tombstone = false;
          route(std::move(s));
          break;
        }

        case JournalOp::kDelete: {
          StagedOp s;
          s.key = read_key(r);
          s.tombstone = true;
          route(std::move(s));
          break;
        }

        case JournalOp::kCommit:
          if (in_txn) {
            for (const StagedOp& s : staged) {
              apply_committed(s);
              ++db.committed_ops;
            }
            staged.clear();
            in_txn = false;
          }
          // A COMMIT with no open txn is a no-op (tolerant of a stray marker).
          break;

        case JournalOp::kRollback:
          if (in_txn) {
            db.rolled_back_ops += static_cast<std::uint32_t>(staged.size());
            staged.clear();
            in_txn = false;
          }
          break;

        default:
          // An unknown op leaves the cursor mid-stream with no way to know the
          // entry width; stop replaying here but keep whatever already
          // committed. (A corrupt journal tail must not discard a valid prefix.)
          stop = true;
          break;
      }
    }
  }

  // A transaction still open at EOF never reached COMMIT: its staged ops are
  // implicitly rolled back (WAL semantics - only committed records are durable).
  if (in_txn) {
    db.rolled_back_ops += static_cast<std::uint32_t>(staged.size());
    staged.clear();
  }

  // Decode the settled committed store into the public record model, resolving
  // STRING_REFs against the string table parsed in stage 3.
  for (const auto& kv : committed) {
    Record rec = decode_record(kv.second.data(), kv.second.size(),
                               db.string_table);
    db.records.emplace(kv.first, std::move(rec));
  }
}

}  // namespace minidb
}  // namespace bw
