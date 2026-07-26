import { describe, expect, it } from "vitest";

import {
  combineSnapshots,
  decodeCommandResponse,
  decodeLedgerResponse,
  decodeMoney,
  decodeProblem,
  decodeSettlementsResponse,
  decodeStateResponse,
  SnapshotMismatchError,
} from "./decode";
import { snapshotAfterLoad } from "./use-backbook";
import type { SnapshotLoadResult } from "./use-backbook";

function stateEnvelope(version = "7", fingerprint = "0x21bd5cac4ef6e98d") {
  return {
    stateVersion: version,
    stateFingerprint: fingerprint,
    data: {
      trades: [
        {
          tradeId: "TRD-1001",
          version: "2",
          state: "CONFIRMED",
          bookId: "BOOK-FX-1",
          counterpartyId: "CPTY-A",
          nettingSetId: "NET-A",
          terms: {
            kind: "FX_SPOT",
            tradeDate: "2026-07-25",
            valueDate: "2026-07-29",
            pay: { currency: "USD", minorUnits: "10125000" },
            receive: { currency: "JPY", minorUnits: "15187500" },
          },
          supersedes: "1",
          supersededBy: null,
        },
      ],
      limits: [],
    },
  };
}

function ledgerEnvelope(version = "7", fingerprint = "0x21bd5cac4ef6e98d") {
  return {
    stateVersion: version,
    stateFingerprint: fingerprint,
    data: {
      balanced: true,
      totals: [
        { currency: "USD", minorUnits: "0" },
        { currency: "JPY", minorUnits: "0" },
        { currency: "KWD", minorUnits: "0" },
      ],
      entries: [],
    },
  };
}

function settlementEnvelope(
  version = "7",
  fingerprint = "0x21bd5cac4ef6e98d",
) {
  return {
    stateVersion: version,
    stateFingerprint: fingerprint,
    data: {
      obligations: [
        {
          counterpartyId: "CPTY-B",
          nettingSetId: "NET-B",
          valueDate: "2026-07-27",
          direction: "INCOMING",
          amount: { currency: "KWD", minorUnits: "35750125" },
        },
      ],
    },
  };
}

describe("decodeMoney", () => {
  it("converts validated minor-unit strings to bigint", () => {
    expect(
      decodeMoney({ currency: "USD", minorUnits: "10125000" }).minorUnits,
    ).toBe(10_125_000n);
    expect(decodeMoney({ currency: "JPY", minorUnits: "0" }).minorUnits).toBe(
      0n,
    );
    expect(
      decodeMoney({ currency: "KWD", minorUnits: "-35750125" }).minorUnits,
    ).toBe(-35_750_125n);
  });

  it("rejects numeric, non-canonical, and unsupported wire values", () => {
    expect(() =>
      decodeMoney({ currency: "USD", minorUnits: 10_125_000 }),
    ).toThrow();
    expect(() =>
      decodeMoney({ currency: "USD", minorUnits: "010" }),
    ).toThrow();
    expect(() =>
      decodeMoney({ currency: "EUR", minorUnits: "10" }),
    ).toThrow();
  });
});

describe("read envelope decoding", () => {
  it("assembles only matching immutable snapshots", () => {
    const snapshot = combineSnapshots(
      decodeStateResponse(stateEnvelope()),
      decodeLedgerResponse(ledgerEnvelope()),
      decodeSettlementsResponse(settlementEnvelope()),
    );

    expect(snapshot.stateVersion).toBe("7");
    expect(snapshot.stateFingerprint).toBe("0x21bd5cac4ef6e98d");
    expect(snapshot.state.trades[0]?.terms.pay.minorUnits).toBe(10_125_000n);
    expect(snapshot.settlements.obligations[0]?.amount.minorUnits).toBe(
      35_750_125n,
    );
  });

  it("rejects version or fingerprint disagreement", () => {
    expect(() =>
      combineSnapshots(
        decodeStateResponse(stateEnvelope()),
        decodeLedgerResponse(ledgerEnvelope("8")),
        decodeSettlementsResponse(settlementEnvelope()),
      ),
    ).toThrow(SnapshotMismatchError);

    expect(() =>
      combineSnapshots(
        decodeStateResponse(stateEnvelope()),
        decodeLedgerResponse(ledgerEnvelope()),
        decodeSettlementsResponse(
          settlementEnvelope("7", "0x0000000000000000"),
        ),
      ),
    ).toThrow(SnapshotMismatchError);
  });

  it("retains the last coherent snapshot after a mismatch", () => {
    const current = combineSnapshots(
      decodeStateResponse(stateEnvelope()),
      decodeLedgerResponse(ledgerEnvelope()),
      decodeSettlementsResponse(settlementEnvelope()),
    );
    const mismatch: SnapshotLoadResult = {
      ok: false,
      mismatch: true,
      error: { kind: "protocol", message: "Snapshot mismatch." },
    };

    expect(snapshotAfterLoad(current, mismatch)).toBe(current);
  });
});

describe("problem decoding", () => {
  it("preserves structured limit-breach evidence", () => {
    const problem = decodeProblem({
      type: "urn:backbook:problem:LIMIT_BREACH",
      title: "Settlement limit breached",
      status: 409,
      detail: "The outgoing cashflow exceeds remaining settlement headroom.",
      code: "LIMIT_BREACH",
      nodePath: ["GROUP", "CPTY-A", "NET-A", "BOOK-FX-1"],
      currency: "USD",
      required: { currency: "USD", minorUnits: "6000000" },
      remaining: { currency: "USD", minorUnits: "4875000" },
    });

    expect(problem.code).toBe("LIMIT_BREACH");
    expect(problem.nodePath?.at(-1)).toBe("BOOK-FX-1");
    expect(problem.required?.minorUnits).toBe(6_000_000n);
    expect(problem.remaining?.minorUnits).toBe(4_875_000n);
  });

  it("accepts every documented problem code and rejects unknown codes", () => {
    const codes = [
      "VALIDATION_FAILED",
      "NOT_FOUND",
      "ILLEGAL_TRANSITION",
      "VERSION_CONFLICT",
      "LIMIT_BREACH",
      "IDEMPOTENCY_CONFLICT",
      "JOURNAL_UNAVAILABLE",
      "INTERNAL_ERROR",
    ];
    for (const code of codes) {
      expect(
        decodeProblem({
          type: `urn:backbook:problem:${code}`,
          title: "Problem",
          status: 409,
          detail: "Request rejected.",
          code,
        }).code,
      ).toBe(code);
    }

    expect(() =>
      decodeProblem({
        type: "urn:backbook:problem:UNKNOWN",
        title: "Unknown",
        status: 500,
        detail: "Unknown problem.",
        code: "UNKNOWN",
      }),
    ).toThrow(/unsupported problem code/);
  });
});

describe("command response decoding", () => {
  it("decodes every result variant from unknown input", () => {
    const responses = [
      {
        idempotentReplay: false,
        result: {
          type: "TRADE_BOOKED",
          tradeId: "TRD-1001",
          version: "1",
          stateVersion: "1",
        },
      },
      {
        idempotentReplay: false,
        result: {
          type: "TRADE_CONFIRMED",
          tradeId: "TRD-1001",
          version: "1",
          stateVersion: "2",
        },
      },
      {
        idempotentReplay: false,
        result: {
          type: "TRADE_AMENDED",
          tradeId: "TRD-1001",
          supersededVersion: "1",
          replacementVersion: "2",
          stateVersion: "3",
        },
      },
      {
        idempotentReplay: false,
        result: {
          type: "TRADE_CANCELLED",
          tradeId: "TRD-1002",
          version: "1",
          stateVersion: "4",
        },
      },
      {
        idempotentReplay: true,
        result: {
          type: "EOD_RUN",
          asOfDate: "2026-07-27",
          settledTradeCount: "2",
          stateVersion: "5",
        },
      },
    ];

    expect(responses.map((response) => decodeCommandResponse(response).result.type))
      .toEqual([
        "TRADE_BOOKED",
        "TRADE_CONFIRMED",
        "TRADE_AMENDED",
        "TRADE_CANCELLED",
        "EOD_RUN",
      ]);
  });
});
