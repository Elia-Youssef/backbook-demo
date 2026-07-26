declare const brand: unique symbol;

// Brands keep wire-format strings distinct after runtime validation.
export type BrandedString<Name extends string> = string & {
  readonly [brand]: Name;
};

export type TradeId = BrandedString<"TradeId">;
export type BookId = BrandedString<"BookId">;
export type CounterpartyId = BrandedString<"CounterpartyId">;
export type NettingSetId = BrandedString<"NettingSetId">;
export type CommandId = BrandedString<"CommandId">;
export type PostingId = BrandedString<"PostingId">;
export type IsoDate = BrandedString<"IsoDate">;
export type Revision = BrandedString<"Revision">;
export type Fingerprint = BrandedString<"Fingerprint">;
export type DecimalString = BrandedString<"DecimalString">;

export type Currency = "USD" | "JPY" | "KWD";
export type InstrumentKind = "FX_SPOT" | "FX_FORWARD";
export type TradeState =
  | "CAPTURED"
  | "CONFIRMED"
  | "SUPERSEDED"
  | "CANCELLED"
  | "SETTLED";
export type PostingSide = "DEBIT" | "CREDIT";
export type SettlementDirection = "OUTGOING" | "INCOMING";
export type LimitLevel = "GROUP" | "COUNTERPARTY" | "NETTING_SET" | "BOOK";

export interface MoneyDto {
  currency: Currency;
  minorUnits: DecimalString;
}

export interface Money {
  currency: Currency;
  minorUnits: bigint;
}

export interface FxTerms {
  kind: InstrumentKind;
  tradeDate: IsoDate;
  valueDate: IsoDate;
  pay: Money;
  receive: Money;
}

export interface Trade {
  tradeId: TradeId;
  version: Revision;
  state: TradeState;
  bookId: BookId;
  counterpartyId: CounterpartyId;
  nettingSetId: NettingSetId;
  terms: FxTerms;
  supersedes: Revision | null;
  supersededBy: Revision | null;
}

export interface LimitBalance {
  level: LimitLevel;
  nodePath: readonly string[];
  currency: Currency;
  capacity: Money;
  reserved: Money;
  headroom: Money;
}

export interface StateData {
  trades: readonly Trade[];
  limits: readonly LimitBalance[];
}

export interface Posting {
  postingId: PostingId;
  tradeId: TradeId;
  tradeVersion: Revision;
  account: string;
  side: PostingSide;
  amount: Money;
  reversalOf: PostingId | null;
}

export interface LedgerEntry {
  entryIndex: Revision;
  postings: readonly Posting[];
}

export interface LedgerData {
  balanced: boolean;
  totals: readonly Money[];
  entries: readonly LedgerEntry[];
}

export interface SettlementObligation {
  counterpartyId: CounterpartyId;
  nettingSetId: NettingSetId;
  valueDate: IsoDate;
  direction: SettlementDirection;
  amount: Money;
}

export interface SettlementData {
  obligations: readonly SettlementObligation[];
}

export interface ReadEnvelope<Data> {
  stateVersion: Revision;
  stateFingerprint: Fingerprint;
  data: Data;
}

export interface DashboardSnapshot {
  stateVersion: Revision;
  stateFingerprint: Fingerprint;
  state: StateData;
  ledger: LedgerData;
  settlements: SettlementData;
}

export interface FieldViolation {
  field: string;
  message: string;
}

export type ProblemCode =
  | "VALIDATION_FAILED"
  | "NOT_FOUND"
  | "ILLEGAL_TRANSITION"
  | "VERSION_CONFLICT"
  | "LIMIT_BREACH"
  | "IDEMPOTENCY_CONFLICT"
  | "JOURNAL_UNAVAILABLE"
  | "INTERNAL_ERROR";

export interface ProblemDetails {
  type: string;
  title: string;
  status: number;
  detail: string;
  code: ProblemCode;
  violations?: readonly FieldViolation[];
  expectedVersion?: Revision;
  actualVersion?: Revision;
  nodePath?: readonly string[];
  currency?: Currency;
  required?: Money;
  remaining?: Money;
}

export type CommandResult =
  | {
      type: "TRADE_BOOKED" | "TRADE_CONFIRMED" | "TRADE_CANCELLED";
      tradeId: TradeId;
      version: Revision;
      stateVersion: Revision;
    }
  | {
      type: "TRADE_AMENDED";
      tradeId: TradeId;
      supersededVersion: Revision;
      replacementVersion: Revision;
      stateVersion: Revision;
    }
  | {
      type: "EOD_RUN";
      asOfDate: IsoDate;
      settledTradeCount: Revision;
      stateVersion: Revision;
    };

export interface CommandResponse {
  idempotentReplay: boolean;
  result: CommandResult;
}

export interface ConfirmationPostingIds {
  payControlDebit: PostingId;
  payPayableCredit: PostingId;
  receiveReceivableDebit: PostingId;
  receiveControlCredit: PostingId;
}

export interface ReversalPostingIds {
  payControlCredit: PostingId;
  payPayableDebit: PostingId;
  receiveReceivableCredit: PostingId;
  receiveControlDebit: PostingId;
}

export interface FxTermsRequest {
  kind: InstrumentKind;
  tradeDate: IsoDate;
  valueDate: IsoDate;
  pay: MoneyDto;
  receive: MoneyDto;
}

export type CommandEnvelope =
  | {
      commandId: CommandId;
      type: "BOOK_TRADE";
      payload: {
        tradeId: TradeId;
        bookId: BookId;
        counterpartyId: CounterpartyId;
        nettingSetId: NettingSetId;
        terms: FxTermsRequest;
      };
    }
  | {
      commandId: CommandId;
      type: "CONFIRM_TRADE";
      expectedVersion: Revision;
      payload: {
        tradeId: TradeId;
        postingIds: ConfirmationPostingIds;
      };
    }
  | {
      commandId: CommandId;
      type: "AMEND_TRADE";
      expectedVersion: Revision;
      payload: {
        tradeId: TradeId;
        replacementTerms: FxTermsRequest;
        reversalPostingIds: ReversalPostingIds;
        replacementPostingIds: ConfirmationPostingIds;
      };
    }
  | {
      commandId: CommandId;
      type: "CANCEL_TRADE";
      expectedVersion: Revision;
      payload: {
        tradeId: TradeId;
        reversalPostingIds?: ReversalPostingIds;
      };
    }
  | {
      commandId: CommandId;
      type: "RUN_EOD";
      payload: {
        asOfDate: IsoDate;
      };
    };

export type ClientError =
  | { kind: "problem"; problem: ProblemDetails }
  | { kind: "transport"; reason: "network" | "timeout"; message: string }
  | { kind: "protocol"; message: string };

export type ClientResult<Value> =
  | { ok: true; status: number; value: Value }
  | { ok: false; status?: number; error: ClientError };

export type ConnectionStatus =
  | "CONNECTING"
  | "LIVE"
  | "REFRESHING"
  | "STALE"
  | "OFFLINE";
