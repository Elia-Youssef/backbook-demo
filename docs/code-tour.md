# Code tour

This tour follows one command from the HTTP boundary to durable storage and
back to the browser. It is intended as a short reading path through the
implementation rather than a complete API reference.

The central idea is:

```text
untrusted JSON
  -> validated command
  -> pure prospective state
  -> one durable journal frame
  -> immutable published snapshot
  -> coherent browser view
```

If a command fails before the journal append, none of the prospective state is
published.

## Repository map

| Area | Start here | Responsibility |
| --- | --- | --- |
| Process assembly | [`src/server/main.cpp`](../src/server/main.cpp) | CLI validation, journal selection, recovery, demo seed, and server startup |
| HTTP and JSON | [`src/server/http_server.cpp`](../src/server/http_server.cpp), [`src/server/json_codec.cpp`](../src/server/json_codec.cpp) | Transport rules and conversion between JSON and typed values |
| Command transaction | [`src/service/command_service.cpp`](../src/service/command_service.cpp) | Idempotency, serialization, durable append, and snapshot publication |
| Pure evaluation | [`src/service/command_evaluator.cpp`](../src/service/command_evaluator.cpp) | Command-to-state, event, and result mapping |
| Domain state | [`src/domain/state.cpp`](../src/domain/state.cpp) | Trade lifecycle, limits, postings, amendments, cancellation, and EOD |
| Journal and recovery | [`src/journal/codec.cpp`](../src/journal/codec.cpp), [`src/journal/replay.cpp`](../src/journal/replay.cpp) | Portable frames, validation, and deterministic state reconstruction |
| Browser data flow | [`web/src/use-backbook.ts`](../web/src/use-backbook.ts) | Coherent reads, polling, write coordination, and stale-state handling |

## 1. Process assembly and recovery

Start with `run` in [`src/server/main.cpp`](../src/server/main.cpp).

The executable:

1. validates CLI options and requires explicit permission for a non-loopback
   bind;
2. constructs the configured limit hierarchy;
3. chooses an isolated demo journal or the requested journal path;
4. gives a `FileJournalStore` to `CommandService::create`;
5. optionally submits every line of
   [`demo/day1.jsonl`](../demo/day1.jsonl) through the normal command boundary;
6. mounts the committed frontend and starts the local HTTP listener.

`CommandService::create` is also the recovery entry point. It reads and scans
the journal, validates the canonical request stored in every batch, replays the
events into a fresh `State`, rebuilds the command-ID index, and publishes the
recovered immutable snapshot. The server does not begin listening if recovery
fails.

The broad exception handler in `main` is the last safety boundary. Typed domain
and service failures are handled before this point; an unexpected boundary
failure produces a generic message without exposing internal details.

## 2. From JSON to a typed command

`HttpServer::Implementation::install_routes` in
[`src/server/http_server.cpp`](../src/server/http_server.cpp) owns the five
routes. The command route first enforces the content type and 64 KiB body
limit, then passes the body to `decode_command_request`.

[`src/server/json_codec.cpp`](../src/server/json_codec.cpp) treats parsed JSON
as untrusted input. It validates object shape, required fields, identifiers,
dates, currencies, money strings, expected versions, and posting IDs before it
constructs a `CommandEnvelope`. Malformed JSON is distinct from a well-formed
document containing invalid fields.

The HTTP layer makes no trade decision. It calls
`CommandService::execute` and maps typed service failures to structured problem
responses. This keeps transport status codes outside the domain library.

## 3. The transactional command boundary

Read `CommandService::execute` in
[`src/service/command_service.cpp`](../src/service/command_service.cpp) from top
to bottom. Its order is the core transaction:

1. Acquire the single writer mutex.
2. Encode the complete command envelope with `canonical_command_bytes`.
3. Check the durable command-ID index.
4. Atomically load the current immutable snapshot.
5. Evaluate the command into a complete prospective state.
6. Build one `CommandBatch` containing the request, event, and logical result.
7. Encode the batch as one journal frame.
8. Prepare the new snapshot and idempotency record.
9. Append and flush the frame.
10. Publish the idempotency record and new snapshot.

The canonical request includes a format version, the command ID, the command
variant tag, and every command field. Repeating the same ID with identical
bytes returns the original result without another append. Reusing the ID with
different bytes returns an idempotency conflict.

All ordinary allocations needed after evaluation are prepared before the
append. Once `append_and_flush` succeeds, publishing the already-prepared
objects cannot expose a half-applied command. If the append or flush fails, the
store becomes unavailable and the old snapshot remains published.

The mutex orders writers, but readers do not take it. They atomically load a
`shared_ptr<const State>` and therefore see either the complete old snapshot or
the complete new snapshot.

## 4. Pure command evaluation

[`src/service/command_evaluator.cpp`](../src/service/command_evaluator.cpp)
contains `evaluate_command`. It has no store and publishes no snapshot. One
exhaustive `std::visit` maps each command variant to:

- a domain state transition;
- the corresponding journal event;
- the result returned to the caller.

`complete_transition` rejects typed domain failures and independently verifies
ledger totals and limit balances before returning `CommandEvaluation`.

This separation makes the business mapping directly testable. The
characterization test in
[`tests/unit/command_service_tests.cpp`](../tests/unit/command_service_tests.cpp)
pins the event tags, result tags, canonical request bytes, state versions, and
final fingerprint across every command variant. Nearby evaluator tests exercise
the same mapping without constructing a journal store.

## 5. Immutable domain transitions

[`src/domain/state.cpp`](../src/domain/state.cpp) contains the state-changing
operations. Each function receives a `const State&`, performs validation, and
returns a new `State`. A failed outcome leaves the caller's state untouched.

The important paths are:

- `book` creates version 1 in `Captured`.
- `confirm` validates the expected version and lifecycle, reserves the outgoing
  cashflow, builds a balanced confirmation entry, and then updates a copied
  state.
- `amend` releases the old reservation prospectively, creates linked
  superseded and replacement trade versions, checks replacement headroom,
  builds exact reversals and replacement postings, and returns all changes
  together.
- `cancel` creates no reversal for a captured trade; a confirmed trade requires
  reversal IDs and releases its reservation.
- `eod` settles eligible confirmed versions, releases their reservations, and
  rebuilds bilateral obligations from all settled versions.

`require_current` is the common optimistic-concurrency check for trade version
commands. `append_entry` enforces posting-ID uniqueness across the whole state
and updates checked running ledger totals. The state version advances only
after a complete transition. An accepted EOD command with no eligible trades is
still journaled but deliberately leaves the state version unchanged.

## 6. Ledger and limit invariants

The accounting boundary is `LedgerEntry::create` in
[`src/domain/ledger.cpp`](../src/domain/ledger.cpp). It refuses an empty entry,
duplicate posting IDs, invalid sides, arithmetic overflow, and an imbalance in
even one currency. USD debits cannot offset JPY credits.

[`src/domain/posting_policy.cpp`](../src/domain/posting_policy.cpp) constructs
the four confirmation postings:

- outgoing cashflow: counterparty-control debit and settlement-payable credit;
- incoming cashflow: settlement-receivable debit and counterparty-control
  credit.

A reversal keeps the original trade, version, account, currency, and amount,
swaps debit with credit, and records the original posting ID in `reversal_of`.

[`src/domain/limits.cpp`](../src/domain/limits.cpp) models:

```text
Group -> Counterparty -> NettingSet -> Book
```

`reserve` checks every node from root to leaf before changing a copied
hierarchy. This both reports the first failing node and prevents partial
reservation. `release` uses the same validate-then-copy pattern. Because the
service serializes writers, two confirmations competing for one remaining
reservation cannot both succeed.

## 7. Business dates and settlement

[`src/domain/business_calendar.cpp`](../src/domain/business_calendar.cpp)
contains the pure date calculation. `calculate_t_plus_two` counts from the day
after the trade date and advances only when both explicit calendars are open.
`adjust_modified_following` moves forward within the original month and rolls
backward if following would cross the month boundary.

The command service continues to accept and journal an explicit value date.
It does not silently select calendars or recalculate a submitted trade.
Callers can use the pure calendar function to produce the value date before
constructing `FxTerms`.

During EOD, [`src/domain/settlement.cpp`](../src/domain/settlement.cpp) groups
settled cashflows by:

```text
(counterparty, netting set, value date, currency)
```

Pay cashflows are outgoing and receive cashflows are incoming from the book's
perspective. Opposite cashflows cancel before the remaining magnitude is
summed, avoiding an order-dependent intermediate overflow. Zero nets are
omitted, and the final obligations are stable-sorted for deterministic API
responses and fingerprints.

## 8. Journal commit and deterministic replay

A `CommandBatch` in
[`include/backbook/journal/command_batch.hpp`](../include/backbook/journal/command_batch.hpp)
keeps four durable facts together:

- a monotonically increasing sequence;
- the command ID and complete canonical request;
- exactly one accepted event;
- the logical result needed for an idempotent replay response.

[`src/journal/codec.cpp`](../src/journal/codec.cpp) encodes explicit tags,
fixed-width little-endian integers, length-prefixed values, and a CRC32-protected
frame. It never writes native object layouts or container iteration order.

[`src/storage/journal_store.cpp`](../src/storage/journal_store.cpp) provides the
file and memory implementations. Recovery truncates only an incomplete final
frame. A complete frame with a bad CRC or unsupported format is fatal rather
than being disguised as a torn write.

[`src/journal/replay.cpp`](../src/journal/replay.cpp) starts from fresh limits,
requires exact batch sequence, applies each event through the same domain
functions, and proves that the stored result describes the resulting state.
It finishes by recomputing ledger totals independently.

[`src/journal/fingerprint.cpp`](../src/journal/fingerprint.cpp) writes an
ordered, versioned export of the logical state and applies FNV-1a 64-bit. The
fingerprint is a deterministic comparison value, not a security mechanism.

## 9. From an immutable snapshot to the browser

Each GET route in [`src/server/http_server.cpp`](../src/server/http_server.cpp)
loads one immutable snapshot and uses it for the entire response. The response
includes the state version and fingerprint as strings.

The three business reads are separate requests, so a writer can commit between
them. [`web/src/use-backbook.ts`](../web/src/use-backbook.ts) requests state,
ledger, and settlements concurrently and
[`web/src/decode.ts`](../web/src/decode.ts) accepts the set only when all three
versions and fingerprints match. One mismatch is retried immediately; a second
failure keeps the last coherent snapshot visible and marks it stale.

Every response begins as `unknown` at the TypeScript boundary. Runtime decoders
validate its complete shape, and monetary minor-unit strings become `BigInt`
only after validation. Transport, problem-response, and protocol failures
remain distinct client results.

[`web/src/polling.ts`](../web/src/polling.ts) keeps at most one refresh in
flight, pauses while the page is hidden, and backs off after failures.
[`web/src/commands.ts`](../web/src/commands.ts) preserves the exact command and
posting IDs when an unchanged write is retried.

## 10. Trace the canonical scenario

The fastest concrete walkthrough is
[`demo/day1.jsonl`](../demo/day1.jsonl):

| Step | Command | Result |
| --- | --- | --- |
| 1 | Book `TRD-1001` | Version 1 is captured |
| 2 | Confirm version 1 | USD headroom is reserved and four postings are created |
| 3 | Amend to version 2 | Version 1 is superseded; reversal and replacement entries are added atomically |
| 4 | Book `TRD-1002` | The second trade remains captured |
| 5 | Confirm `TRD-1002` | The book-level USD limit rejects the command with no journal append |
| 6 | Book `TRD-1003` | A JPY/KWD forward is captured |
| 7 | Confirm `TRD-1003` | Its outgoing JPY is reserved and posted |
| 8 | Run EOD | `TRD-1003` settles and produces outgoing JPY and incoming KWD obligations |

[`src/server/demo.cpp`](../src/server/demo.cpp) sends these lines through the
same JSON decoder and command service as HTTP, then verifies the accepted and
rejected counts, trade history, headroom, ledger totals, settlements, and
canonical fingerprint.

## 11. Tests to read beside the code

| Question | Focused evidence |
| --- | --- |
| Can an unbalanced ledger entry exist? | [`tests/unit/ledger_tests.cpp`](../tests/unit/ledger_tests.cpp) |
| Does a limit failure leave state untouched? | [`tests/unit/state_tests.cpp`](../tests/unit/state_tests.cpp) |
| Is amendment one reversal-plus-rebook operation? | [`tests/unit/state_tests.cpp`](../tests/unit/state_tests.cpp) |
| Is T+2 correct across both calendars? | [`tests/unit/business_calendar_tests.cpp`](../tests/unit/business_calendar_tests.cpp) |
| Is replay deterministic and corruption explicit? | [`tests/unit/journal_replay_tests.cpp`](../tests/unit/journal_replay_tests.cpp), [`tests/unit/journal_storage_tests.cpp`](../tests/unit/journal_storage_tests.cpp) |
| Can two writers over-reserve the same headroom? | [`tests/unit/command_service_tests.cpp`](../tests/unit/command_service_tests.cpp) |
| Can readers observe a partial command? | [`tests/unit/command_service_tests.cpp`](../tests/unit/command_service_tests.cpp) |
| Does the HTTP boundary preserve safe errors and coherent reads? | [`tests/http/http_server_tests.cpp`](../tests/http/http_server_tests.cpp) |
| Does the browser reject inconsistent or imprecise data? | [`web/src/decode.test.ts`](../web/src/decode.test.ts), [`web/src/money.test.ts`](../web/src/money.test.ts) |

For target dependencies and the formal commit sequence, continue with
[`architecture.md`](architecture.md). For the exact compiler and runtime
evidence, see [`verification.md`](verification.md).
