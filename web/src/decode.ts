import type {
  BookId,
  CommandId,
  CommandResponse,
  CounterpartyId,
  Currency,
  DashboardSnapshot,
  DecimalString,
  FieldViolation,
  Fingerprint,
  InstrumentKind,
  IsoDate,
  LedgerData,
  LedgerEntry,
  LimitBalance,
  LimitLevel,
  Money,
  NettingSetId,
  Posting,
  PostingId,
  PostingSide,
  ProblemCode,
  ProblemDetails,
  ReadEnvelope,
  Revision,
  SettlementData,
  SettlementDirection,
  SettlementObligation,
  StateData,
  Trade,
  TradeId,
  TradeState,
} from "./contracts";

const identifierPattern = /^[A-Za-z0-9][A-Za-z0-9._:-]{0,63}$/;
const decimalPattern = /^-?(0|[1-9][0-9]*)$/;
const unsignedPattern = /^(0|[1-9][0-9]*)$/;
const datePattern = /^[0-9]{4}-[0-9]{2}-[0-9]{2}$/;
const fingerprintPattern = /^0x[0-9a-f]{16}$/;

export class ProtocolDecodeError extends Error {}

export class SnapshotMismatchError extends Error {}

type UnknownRecord = Record<string, unknown>;

function record(value: unknown, path: string): UnknownRecord {
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    throw new ProtocolDecodeError(`${path} must be an object`);
  }
  return value as UnknownRecord;
}

function member(value: UnknownRecord, name: string, path: string): unknown {
  if (!(name in value)) {
    throw new ProtocolDecodeError(`${path}.${name} is required`);
  }
  return value[name];
}

function stringValue(value: unknown, path: string): string {
  if (typeof value !== "string") {
    throw new ProtocolDecodeError(`${path} must be a string`);
  }
  return value;
}

function booleanValue(value: unknown, path: string): boolean {
  if (typeof value !== "boolean") {
    throw new ProtocolDecodeError(`${path} must be a boolean`);
  }
  return value;
}

function integerValue(value: unknown, path: string): number {
  if (typeof value !== "number" || !Number.isSafeInteger(value)) {
    throw new ProtocolDecodeError(`${path} must be an integer`);
  }
  return value;
}

function arrayValue(value: unknown, path: string): readonly unknown[] {
  if (!Array.isArray(value)) {
    throw new ProtocolDecodeError(`${path} must be an array`);
  }
  return value;
}

function identifier(value: unknown, path: string): string {
  const text = stringValue(value, path);
  if (!identifierPattern.test(text)) {
    throw new ProtocolDecodeError(`${path} is not a valid identifier`);
  }
  return text;
}

export function decodeTradeId(value: unknown, path = "$"): TradeId {
  return identifier(value, path) as TradeId;
}

export function decodeBookId(value: unknown, path = "$"): BookId {
  return identifier(value, path) as BookId;
}

export function decodeCounterpartyId(
  value: unknown,
  path = "$",
): CounterpartyId {
  return identifier(value, path) as CounterpartyId;
}

export function decodeNettingSetId(
  value: unknown,
  path = "$",
): NettingSetId {
  return identifier(value, path) as NettingSetId;
}

export function decodePostingId(value: unknown, path = "$"): PostingId {
  return identifier(value, path) as PostingId;
}

export function decodeCommandId(value: unknown, path = "$"): CommandId {
  return identifier(value, path) as CommandId;
}

export function decodeRevision(value: unknown, path = "$"): Revision {
  const text = stringValue(value, path);
  if (!unsignedPattern.test(text)) {
    throw new ProtocolDecodeError(
      `${path} must be a canonical unsigned decimal string`,
    );
  }
  return text as Revision;
}

export function decodeDecimalString(
  value: unknown,
  path = "$",
): DecimalString {
  const text = stringValue(value, path);
  if (!decimalPattern.test(text)) {
    throw new ProtocolDecodeError(
      `${path} must be a canonical signed decimal string`,
    );
  }
  return text as DecimalString;
}

export function decodeIsoDate(value: unknown, path = "$"): IsoDate {
  const text = stringValue(value, path);
  if (!datePattern.test(text)) {
    throw new ProtocolDecodeError(`${path} must be an ISO date`);
  }
  const parsed = new Date(`${text}T00:00:00Z`);
  if (Number.isNaN(parsed.valueOf()) || parsed.toISOString().slice(0, 10) !== text) {
    throw new ProtocolDecodeError(`${path} must be a real ISO date`);
  }
  return text as IsoDate;
}

function decodeFingerprint(value: unknown, path: string): Fingerprint {
  const text = stringValue(value, path);
  if (!fingerprintPattern.test(text)) {
    throw new ProtocolDecodeError(`${path} must be a canonical fingerprint`);
  }
  return text as Fingerprint;
}

function decodeCurrency(value: unknown, path: string): Currency {
  const text = stringValue(value, path);
  if (text === "USD" || text === "JPY" || text === "KWD") {
    return text;
  }
  throw new ProtocolDecodeError(`${path} contains an unsupported currency`);
}

export function decodeMoney(value: unknown, path = "$"): Money {
  const source = record(value, path);
  const currency = decodeCurrency(member(source, "currency", path), `${path}.currency`);
  const minorUnits = decodeDecimalString(
    member(source, "minorUnits", path),
    `${path}.minorUnits`,
  );
  return { currency, minorUnits: BigInt(minorUnits) };
}

function decodeInstrumentKind(value: unknown, path: string): InstrumentKind {
  const text = stringValue(value, path);
  if (text === "FX_SPOT" || text === "FX_FORWARD") {
    return text;
  }
  throw new ProtocolDecodeError(`${path} contains an unsupported instrument`);
}

function decodeTradeState(value: unknown, path: string): TradeState {
  const text = stringValue(value, path);
  if (
    text === "CAPTURED" ||
    text === "CONFIRMED" ||
    text === "SUPERSEDED" ||
    text === "CANCELLED" ||
    text === "SETTLED"
  ) {
    return text;
  }
  throw new ProtocolDecodeError(`${path} contains an unsupported trade state`);
}

function decodeLimitLevel(value: unknown, path: string): LimitLevel {
  const text = stringValue(value, path);
  if (
    text === "GROUP" ||
    text === "COUNTERPARTY" ||
    text === "NETTING_SET" ||
    text === "BOOK"
  ) {
    return text;
  }
  throw new ProtocolDecodeError(`${path} contains an unsupported limit level`);
}

function decodePostingSide(value: unknown, path: string): PostingSide {
  const text = stringValue(value, path);
  if (text === "DEBIT" || text === "CREDIT") {
    return text;
  }
  throw new ProtocolDecodeError(`${path} contains an unsupported posting side`);
}

function decodeDirection(
  value: unknown,
  path: string,
): SettlementDirection {
  const text = stringValue(value, path);
  if (text === "OUTGOING" || text === "INCOMING") {
    return text;
  }
  throw new ProtocolDecodeError(
    `${path} contains an unsupported settlement direction`,
  );
}

function nullableRevision(value: unknown, path: string): Revision | null {
  return value === null ? null : decodeRevision(value, path);
}

function nullablePostingId(value: unknown, path: string): PostingId | null {
  return value === null ? null : decodePostingId(value, path);
}

function decodeTrade(value: unknown, path: string): Trade {
  const source = record(value, path);
  const terms = record(member(source, "terms", path), `${path}.terms`);
  return {
    tradeId: decodeTradeId(member(source, "tradeId", path), `${path}.tradeId`),
    version: decodeRevision(member(source, "version", path), `${path}.version`),
    state: decodeTradeState(member(source, "state", path), `${path}.state`),
    bookId: decodeBookId(member(source, "bookId", path), `${path}.bookId`),
    counterpartyId: decodeCounterpartyId(
      member(source, "counterpartyId", path),
      `${path}.counterpartyId`,
    ),
    nettingSetId: decodeNettingSetId(
      member(source, "nettingSetId", path),
      `${path}.nettingSetId`,
    ),
    terms: {
      kind: decodeInstrumentKind(
        member(terms, "kind", `${path}.terms`),
        `${path}.terms.kind`,
      ),
      tradeDate: decodeIsoDate(
        member(terms, "tradeDate", `${path}.terms`),
        `${path}.terms.tradeDate`,
      ),
      valueDate: decodeIsoDate(
        member(terms, "valueDate", `${path}.terms`),
        `${path}.terms.valueDate`,
      ),
      pay: decodeMoney(
        member(terms, "pay", `${path}.terms`),
        `${path}.terms.pay`,
      ),
      receive: decodeMoney(
        member(terms, "receive", `${path}.terms`),
        `${path}.terms.receive`,
      ),
    },
    supersedes: nullableRevision(
      member(source, "supersedes", path),
      `${path}.supersedes`,
    ),
    supersededBy: nullableRevision(
      member(source, "supersededBy", path),
      `${path}.supersededBy`,
    ),
  };
}

function decodeLimit(value: unknown, path: string): LimitBalance {
  const source = record(value, path);
  const nodePath = arrayValue(member(source, "nodePath", path), `${path}.nodePath`).map(
    (part, index) => stringValue(part, `${path}.nodePath[${index}]`),
  );
  return {
    level: decodeLimitLevel(member(source, "level", path), `${path}.level`),
    nodePath,
    currency: decodeCurrency(member(source, "currency", path), `${path}.currency`),
    capacity: decodeMoney(member(source, "capacity", path), `${path}.capacity`),
    reserved: decodeMoney(member(source, "reserved", path), `${path}.reserved`),
    headroom: decodeMoney(member(source, "headroom", path), `${path}.headroom`),
  };
}

function decodeStateData(value: unknown, path: string): StateData {
  const source = record(value, path);
  return {
    trades: arrayValue(member(source, "trades", path), `${path}.trades`).map(
      (trade, index) => decodeTrade(trade, `${path}.trades[${index}]`),
    ),
    limits: arrayValue(member(source, "limits", path), `${path}.limits`).map(
      (limit, index) => decodeLimit(limit, `${path}.limits[${index}]`),
    ),
  };
}

function decodePosting(value: unknown, path: string): Posting {
  const source = record(value, path);
  return {
    postingId: decodePostingId(
      member(source, "postingId", path),
      `${path}.postingId`,
    ),
    tradeId: decodeTradeId(member(source, "tradeId", path), `${path}.tradeId`),
    tradeVersion: decodeRevision(
      member(source, "tradeVersion", path),
      `${path}.tradeVersion`,
    ),
    account: stringValue(member(source, "account", path), `${path}.account`),
    side: decodePostingSide(member(source, "side", path), `${path}.side`),
    amount: decodeMoney(member(source, "amount", path), `${path}.amount`),
    reversalOf: nullablePostingId(
      member(source, "reversalOf", path),
      `${path}.reversalOf`,
    ),
  };
}

function decodeLedgerEntry(value: unknown, path: string): LedgerEntry {
  const source = record(value, path);
  return {
    entryIndex: decodeRevision(
      member(source, "entryIndex", path),
      `${path}.entryIndex`,
    ),
    postings: arrayValue(
      member(source, "postings", path),
      `${path}.postings`,
    ).map((posting, index) =>
      decodePosting(posting, `${path}.postings[${index}]`),
    ),
  };
}

function decodeLedgerData(value: unknown, path: string): LedgerData {
  const source = record(value, path);
  return {
    balanced: booleanValue(member(source, "balanced", path), `${path}.balanced`),
    totals: arrayValue(member(source, "totals", path), `${path}.totals`).map(
      (total, index) => decodeMoney(total, `${path}.totals[${index}]`),
    ),
    entries: arrayValue(member(source, "entries", path), `${path}.entries`).map(
      (entry, index) => decodeLedgerEntry(entry, `${path}.entries[${index}]`),
    ),
  };
}

function decodeObligation(
  value: unknown,
  path: string,
): SettlementObligation {
  const source = record(value, path);
  return {
    counterpartyId: decodeCounterpartyId(
      member(source, "counterpartyId", path),
      `${path}.counterpartyId`,
    ),
    nettingSetId: decodeNettingSetId(
      member(source, "nettingSetId", path),
      `${path}.nettingSetId`,
    ),
    valueDate: decodeIsoDate(
      member(source, "valueDate", path),
      `${path}.valueDate`,
    ),
    direction: decodeDirection(
      member(source, "direction", path),
      `${path}.direction`,
    ),
    amount: decodeMoney(member(source, "amount", path), `${path}.amount`),
  };
}

function decodeSettlementData(value: unknown, path: string): SettlementData {
  const source = record(value, path);
  return {
    obligations: arrayValue(
      member(source, "obligations", path),
      `${path}.obligations`,
    ).map((obligation, index) =>
      decodeObligation(obligation, `${path}.obligations[${index}]`),
    ),
  };
}

function decodeEnvelope<Data>(
  value: unknown,
  decodeData: (source: unknown, path: string) => Data,
): ReadEnvelope<Data> {
  const source = record(value, "$");
  return {
    stateVersion: decodeRevision(member(source, "stateVersion", "$"), "$.stateVersion"),
    stateFingerprint: decodeFingerprint(
      member(source, "stateFingerprint", "$"),
      "$.stateFingerprint",
    ),
    data: decodeData(member(source, "data", "$"), "$.data"),
  };
}

export function decodeStateResponse(value: unknown): ReadEnvelope<StateData> {
  return decodeEnvelope(value, decodeStateData);
}

export function decodeLedgerResponse(value: unknown): ReadEnvelope<LedgerData> {
  return decodeEnvelope(value, decodeLedgerData);
}

export function decodeSettlementsResponse(
  value: unknown,
): ReadEnvelope<SettlementData> {
  return decodeEnvelope(value, decodeSettlementData);
}

function decodeProblemCode(value: unknown, path: string): ProblemCode {
  const text = stringValue(value, path);
  if (
    text === "VALIDATION_FAILED" ||
    text === "NOT_FOUND" ||
    text === "ILLEGAL_TRANSITION" ||
    text === "VERSION_CONFLICT" ||
    text === "LIMIT_BREACH" ||
    text === "IDEMPOTENCY_CONFLICT" ||
    text === "JOURNAL_UNAVAILABLE" ||
    text === "INTERNAL_ERROR"
  ) {
    return text;
  }
  throw new ProtocolDecodeError(`${path} contains an unsupported problem code`);
}

function decodeViolations(value: unknown, path: string): readonly FieldViolation[] {
  return arrayValue(value, path).map((item, index) => {
    const itemPath = `${path}[${index}]`;
    const source = record(item, itemPath);
    return {
      field: stringValue(member(source, "field", itemPath), `${itemPath}.field`),
      message: stringValue(
        member(source, "message", itemPath),
        `${itemPath}.message`,
      ),
    };
  });
}

export function decodeProblem(value: unknown): ProblemDetails {
  const source = record(value, "$");
  const problem: ProblemDetails = {
    type: stringValue(member(source, "type", "$"), "$.type"),
    title: stringValue(member(source, "title", "$"), "$.title"),
    status: integerValue(member(source, "status", "$"), "$.status"),
    detail: stringValue(member(source, "detail", "$"), "$.detail"),
    code: decodeProblemCode(member(source, "code", "$"), "$.code"),
  };
  if ("violations" in source) {
    problem.violations = decodeViolations(source.violations, "$.violations");
  }
  if ("expectedVersion" in source) {
    problem.expectedVersion = decodeRevision(
      source.expectedVersion,
      "$.expectedVersion",
    );
  }
  if ("actualVersion" in source) {
    problem.actualVersion = decodeRevision(
      source.actualVersion,
      "$.actualVersion",
    );
  }
  if ("nodePath" in source) {
    problem.nodePath = arrayValue(source.nodePath, "$.nodePath").map(
      (part, index) => stringValue(part, `$.nodePath[${index}]`),
    );
  }
  if ("currency" in source) {
    problem.currency = decodeCurrency(source.currency, "$.currency");
  }
  if ("required" in source) {
    problem.required = decodeMoney(source.required, "$.required");
  }
  if ("remaining" in source) {
    problem.remaining = decodeMoney(source.remaining, "$.remaining");
  }
  return problem;
}

export function decodeCommandResponse(value: unknown): CommandResponse {
  const source = record(value, "$");
  const result = record(member(source, "result", "$"), "$.result");
  const type = stringValue(member(result, "type", "$.result"), "$.result.type");
  const stateVersion = decodeRevision(
    member(result, "stateVersion", "$.result"),
    "$.result.stateVersion",
  );
  if (
    type === "TRADE_BOOKED" ||
    type === "TRADE_CONFIRMED" ||
    type === "TRADE_CANCELLED"
  ) {
    return {
      idempotentReplay: booleanValue(
        member(source, "idempotentReplay", "$"),
        "$.idempotentReplay",
      ),
      result: {
        type,
        tradeId: decodeTradeId(
          member(result, "tradeId", "$.result"),
          "$.result.tradeId",
        ),
        version: decodeRevision(
          member(result, "version", "$.result"),
          "$.result.version",
        ),
        stateVersion,
      },
    };
  }
  if (type === "TRADE_AMENDED") {
    return {
      idempotentReplay: booleanValue(
        member(source, "idempotentReplay", "$"),
        "$.idempotentReplay",
      ),
      result: {
        type,
        tradeId: decodeTradeId(
          member(result, "tradeId", "$.result"),
          "$.result.tradeId",
        ),
        supersededVersion: decodeRevision(
          member(result, "supersededVersion", "$.result"),
          "$.result.supersededVersion",
        ),
        replacementVersion: decodeRevision(
          member(result, "replacementVersion", "$.result"),
          "$.result.replacementVersion",
        ),
        stateVersion,
      },
    };
  }
  if (type === "EOD_RUN") {
    return {
      idempotentReplay: booleanValue(
        member(source, "idempotentReplay", "$"),
        "$.idempotentReplay",
      ),
      result: {
        type,
        asOfDate: decodeIsoDate(
          member(result, "asOfDate", "$.result"),
          "$.result.asOfDate",
        ),
        settledTradeCount: decodeRevision(
          member(result, "settledTradeCount", "$.result"),
          "$.result.settledTradeCount",
        ),
        stateVersion,
      },
    };
  }
  throw new ProtocolDecodeError("$.result.type is unsupported");
}

export function combineSnapshots(
  state: ReadEnvelope<StateData>,
  ledger: ReadEnvelope<LedgerData>,
  settlements: ReadEnvelope<SettlementData>,
): DashboardSnapshot {
  if (
    state.stateVersion !== ledger.stateVersion ||
    state.stateVersion !== settlements.stateVersion ||
    state.stateFingerprint !== ledger.stateFingerprint ||
    state.stateFingerprint !== settlements.stateFingerprint
  ) {
    throw new SnapshotMismatchError(
      "Read endpoints did not describe the same immutable snapshot.",
    );
  }
  return {
    stateVersion: state.stateVersion,
    stateFingerprint: state.stateFingerprint,
    state: state.data,
    ledger: ledger.data,
    settlements: settlements.data,
  };
}
