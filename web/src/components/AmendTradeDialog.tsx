import { useState } from "react";
import type { FormEvent } from "react";

import {
  newCommandId,
  newConfirmationPostingIds,
  newReversalPostingIds,
} from "../commands";
import type {
  CommandEnvelope,
  Currency,
  InstrumentKind,
  Trade,
} from "../contracts";
import { decodeIsoDate } from "../decode";
import { moneyInputValue, parsePositiveMoney } from "../money";

interface AmendTradeDialogProps {
  trade: Trade;
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

export function AmendTradeDialog({
  trade,
  busy,
  onClose,
  onSubmit,
}: AmendTradeDialogProps) {
  const [kind, setKind] = useState<InstrumentKind>(trade.terms.kind);
  const [tradeDate, setTradeDate] = useState<string>(trade.terms.tradeDate);
  const [valueDate, setValueDate] = useState<string>(trade.terms.valueDate);
  const [payCurrency, setPayCurrency] = useState<Currency>(
    trade.terms.pay.currency,
  );
  const [payAmount, setPayAmount] = useState(
    moneyInputValue(trade.terms.pay),
  );
  const [receiveCurrency, setReceiveCurrency] = useState<Currency>(
    trade.terms.receive.currency,
  );
  const [receiveAmount, setReceiveAmount] = useState(
    moneyInputValue(trade.terms.receive),
  );
  const [validation, setValidation] = useState<string | null>(null);

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
      const command: CommandEnvelope = {
        commandId: newCommandId(),
        type: "AMEND_TRADE",
        expectedVersion: trade.version,
        payload: {
          tradeId: trade.tradeId,
          replacementTerms: {
            kind,
            tradeDate: decodeIsoDate(tradeDate),
            valueDate: decodeIsoDate(valueDate),
            pay,
            receive,
          },
          reversalPostingIds: newReversalPostingIds(),
          replacementPostingIds: newConfirmationPostingIds(),
        },
      };
      setValidation(null);
      if (await onSubmit(command)) {
        onClose();
      }
    } catch {
      setValidation("Review the ISO dates before submitting.");
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
        aria-labelledby="amend-dialog-title"
      >
        <div className="dialog-header">
          <div>
            <p className="eyebrow">
              {trade.tradeId} · v{trade.version}
            </p>
            <h2 id="amend-dialog-title">Amend confirmed trade</h2>
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
        <p className="dialog-note">
          The current version remains queryable. Acceptance commits its exact
          reversal and the confirmed replacement as one command.
        </p>
        <form onSubmit={(event) => void submit(event)}>
          <div className="form-grid">
            <label>
              Instrument
              <select
                autoFocus
                value={kind}
                onChange={(event) => setKind(instrument(event.currentTarget.value))}
              >
                <option value="FX_SPOT">FX spot</option>
                <option value="FX_FORWARD">FX forward</option>
              </select>
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
              {busy ? "Committing…" : "Reverse and rebook"}
            </button>
          </div>
        </form>
      </section>
    </div>
  );
}
