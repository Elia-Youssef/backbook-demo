import { useRef, useState } from "react";
import type { FormEvent } from "react";

import {
  newCommandId,
  prepareCommandSubmission,
} from "../commands";
import type { PreparedCommandSubmission } from "../commands";
import type {
  CommandEnvelope,
  Currency,
  InstrumentKind,
} from "../contracts";
import {
  decodeBookId,
  decodeCounterpartyId,
  decodeIsoDate,
  decodeNettingSetId,
  decodeTradeId,
} from "../decode";
import { parsePositiveMoney } from "../money";

interface BookTradeDialogProps {
  busy: boolean;
  onClose: () => void;
  onSubmit: (command: CommandEnvelope) => Promise<boolean>;
}

function currency(value: string): Currency {
  if (value === "USD" || value === "JPY" || value === "KWD") {
    return value;
  }
  return "USD";
}

function instrument(value: string): InstrumentKind {
  return value === "FX_FORWARD" ? "FX_FORWARD" : "FX_SPOT";
}

export function BookTradeDialog({
  busy,
  onClose,
  onSubmit,
}: BookTradeDialogProps) {
  const [tradeId, setTradeId] = useState(
    () => `TRD-${crypto.randomUUID().slice(0, 8).toUpperCase()}`,
  );
  const [bookId, setBookId] = useState("BOOK-FX-1");
  const [counterpartyId, setCounterpartyId] = useState("CPTY-A");
  const [nettingSetId, setNettingSetId] = useState("NET-A");
  const [kind, setKind] = useState<InstrumentKind>("FX_SPOT");
  const [tradeDate, setTradeDate] = useState("2026-07-25");
  const [valueDate, setValueDate] = useState("2026-07-29");
  const [payCurrency, setPayCurrency] = useState<Currency>("USD");
  const [payAmount, setPayAmount] = useState("25000.00");
  const [receiveCurrency, setReceiveCurrency] = useState<Currency>("JPY");
  const [receiveAmount, setReceiveAmount] = useState("3750000");
  const [validation, setValidation] = useState<string | null>(null);
  const pendingSubmission = useRef<PreparedCommandSubmission | null>(null);

  async function submit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    const pay = parsePositiveMoney(payAmount, payCurrency);
    const receive = parsePositiveMoney(receiveAmount, receiveCurrency);
    if (pay === null || receive === null) {
      setValidation("Enter positive amounts using each currency's precision.");
      return;
    }
    if (payCurrency === receiveCurrency) {
      setValidation("Pay and receive currencies must differ.");
      return;
    }
    try {
      const payload = {
        tradeId: decodeTradeId(tradeId),
        bookId: decodeBookId(bookId),
        counterpartyId: decodeCounterpartyId(counterpartyId),
        nettingSetId: decodeNettingSetId(nettingSetId),
        terms: {
          kind,
          tradeDate: decodeIsoDate(tradeDate),
          valueDate: decodeIsoDate(valueDate),
          pay,
          receive,
        },
      };
      const prepared = prepareCommandSubmission(
        pendingSubmission.current,
        JSON.stringify({ type: "BOOK_TRADE", payload }),
        (): CommandEnvelope => ({
          commandId: newCommandId(),
          type: "BOOK_TRADE",
          payload,
        }),
      );
      pendingSubmission.current = prepared;
      setValidation(null);
      if (await onSubmit(prepared.command)) {
        onClose();
      }
    } catch {
      setValidation("Review identifiers and ISO dates before submitting.");
    }
  }

  return (
    <div
      className="dialog-backdrop"
      role="presentation"
      onKeyDown={(event) => {
        if (event.key === "Escape" && !busy) {
          onClose();
        }
      }}
    >
      <section
        className="dialog"
        role="dialog"
        aria-modal="true"
        aria-labelledby="book-dialog-title"
      >
        <div className="dialog-header">
          <div>
            <p className="eyebrow">New lifecycle</p>
            <h2 id="book-dialog-title">Book FX trade</h2>
          </div>
          <button
            className="dismiss-button"
            type="button"
            onClick={onClose}
            disabled={busy}
          >
            Close
          </button>
        </div>
        <form onSubmit={(event) => void submit(event)}>
          <div className="form-grid">
            <label>
              Trade ID
              <input
                autoFocus
                value={tradeId}
                onChange={(event) => setTradeId(event.currentTarget.value)}
              />
            </label>
            <label>
              Instrument
              <select
                value={kind}
                onChange={(event) => setKind(instrument(event.currentTarget.value))}
              >
                <option value="FX_SPOT">FX spot</option>
                <option value="FX_FORWARD">FX forward</option>
              </select>
            </label>
            <label>
              Counterparty
              <input
                value={counterpartyId}
                onChange={(event) =>
                  setCounterpartyId(event.currentTarget.value)
                }
              />
            </label>
            <label>
              Netting set
              <input
                value={nettingSetId}
                onChange={(event) => setNettingSetId(event.currentTarget.value)}
              />
            </label>
            <label>
              Book
              <input
                value={bookId}
                onChange={(event) => setBookId(event.currentTarget.value)}
              />
            </label>
            <label>
              Trade date
              <input
                type="date"
                value={tradeDate}
                onChange={(event) => setTradeDate(event.currentTarget.value)}
              />
            </label>
            <label>
              Value date
              <input
                type="date"
                value={valueDate}
                onChange={(event) => setValueDate(event.currentTarget.value)}
              />
            </label>
            <div />
            <label>
              Pay amount
              <input
                inputMode="decimal"
                value={payAmount}
                onChange={(event) => setPayAmount(event.currentTarget.value)}
              />
            </label>
            <label>
              Pay currency
              <select
                value={payCurrency}
                onChange={(event) =>
                  setPayCurrency(currency(event.currentTarget.value))
                }
              >
                <option>USD</option>
                <option>JPY</option>
                <option>KWD</option>
              </select>
            </label>
            <label>
              Receive amount
              <input
                inputMode="decimal"
                value={receiveAmount}
                onChange={(event) => setReceiveAmount(event.currentTarget.value)}
              />
            </label>
            <label>
              Receive currency
              <select
                value={receiveCurrency}
                onChange={(event) =>
                  setReceiveCurrency(currency(event.currentTarget.value))
                }
              >
                <option>USD</option>
                <option>JPY</option>
                <option>KWD</option>
              </select>
            </label>
          </div>
          {validation === null ? null : (
            <p className="form-error">{validation}</p>
          )}
          <div className="dialog-actions">
            <button
              className="button button-secondary"
              type="button"
              onClick={onClose}
              disabled={busy}
            >
              Cancel
            </button>
            <button className="button" type="submit" disabled={busy}>
              {busy ? "Committing…" : "Book captured trade"}
            </button>
          </div>
        </form>
      </section>
    </div>
  );
}
