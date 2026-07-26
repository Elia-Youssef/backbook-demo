import { useEffect, useMemo, useRef, useState } from "react";

import {
  newCommandId,
  prepareCommandSubmission,
} from "./commands";
import type { PreparedCommandSubmission } from "./commands";
import { AmendTradeDialog } from "./components/AmendTradeDialog";
import { BlotterTable, tradeKey } from "./components/BlotterTable";
import { BookTradeDialog } from "./components/BookTradeDialog";
import { ProblemPanel } from "./components/ProblemPanel";
import { SettlementPanel } from "./components/SettlementPanel";
import { StatusStrip } from "./components/StatusStrip";
import { TradeInspector } from "./components/TradeInspector";
import type { CommandEnvelope, IsoDate, Trade } from "./contracts";
import { useBackbook } from "./use-backbook";

function abbreviatedFingerprint(fingerprint: string | undefined): string {
  if (fingerprint === undefined) {
    return "—";
  }
  return `${fingerprint.slice(0, 10)}…${fingerprint.slice(-4)}`;
}

function latestConfirmedDate(trades: readonly Trade[]): IsoDate | null {
  const dates = trades
    .filter((trade) => trade.state === "CONFIRMED")
    .map((trade) => trade.terms.valueDate)
    .sort();
  return dates.at(-1) ?? null;
}

export function App() {
  const model = useBackbook();
  const trades = model.snapshot?.state.trades ?? [];
  const [selectedKey, setSelectedKey] = useState<string | null>(null);
  const [bookOpen, setBookOpen] = useState(false);
  const [amendOpen, setAmendOpen] = useState(false);
  const pendingEod = useRef<PreparedCommandSubmission | null>(null);

  const selectedTrade = useMemo(
    () => trades.find((trade) => tradeKey(trade) === selectedKey) ?? null,
    [selectedKey, trades],
  );

  useEffect(() => {
    if (trades.length === 0) {
      setSelectedKey(null);
      return;
    }
    if (trades.some((trade) => tradeKey(trade) === selectedKey)) {
      return;
    }
    const defaultTrade =
      [...trades].reverse().find((trade) => trade.state === "CONFIRMED") ??
      trades[0];
    if (defaultTrade !== undefined) {
      setSelectedKey(tradeKey(defaultTrade));
    }
  }, [selectedKey, trades]);

  const eodDate = latestConfirmedDate(trades);

  async function runEod() {
    if (eodDate === null) {
      return;
    }
    const signature = JSON.stringify({
      type: "RUN_EOD",
      payload: { asOfDate: eodDate },
    });
    const prepared = prepareCommandSubmission(
      pendingEod.current,
      signature,
      (): CommandEnvelope => ({
        commandId: newCommandId(),
        type: "RUN_EOD",
        payload: { asOfDate: eodDate },
      }),
    );
    pendingEod.current = prepared;
    if (
      (await model.execute(prepared.command)) &&
      pendingEod.current === prepared
    ) {
      pendingEod.current = null;
    }
  }

  const loading = model.snapshot === null;
  return (
    <div className="app-shell">
      <header className="title-bar">
        <div className="brand-block">
          <span className="brand-mark" aria-hidden="true">
            BB
          </span>
          <div>
            <h1>Backbook</h1>
            <span>Post-trade operations</span>
          </div>
        </div>
        <div className="connection-metadata">
          <span
            className={`connection-state connection-${model.connection.toLowerCase()}`}
          >
            <span className="connection-dot" aria-hidden="true" />
            {model.connection}
          </span>
          <span>
            State{" "}
            <strong>{model.snapshot?.stateVersion ?? "—"}</strong>
          </span>
          <span className="fingerprint">
            {abbreviatedFingerprint(model.snapshot?.stateFingerprint)}
          </span>
        </div>
      </header>

      <StatusStrip
        totals={model.snapshot?.ledger.totals ?? []}
        balanced={model.snapshot?.ledger.balanced ?? false}
        ready={model.snapshot !== null}
        connection={model.connection}
        writesDisabled={model.writesDisabled}
        writePending={model.writePending}
        canAmend={selectedTrade?.state === "CONFIRMED"}
        eodDate={eodDate}
        onRefresh={() => void model.refresh()}
        onBook={() => setBookOpen(true)}
        onAmend={() => setAmendOpen(true)}
        onEod={() => void runEod()}
      />

      {model.error === null ? null : (
        <ProblemPanel error={model.error} onDismiss={model.clearError} />
      )}

      <main className="main-grid">
        <BlotterTable
          trades={trades}
          selectedKey={selectedKey}
          loading={loading}
          onSelect={(trade) => setSelectedKey(tradeKey(trade))}
          onBook={() => setBookOpen(true)}
        />
        <TradeInspector
          trade={selectedTrade}
          ledger={model.snapshot?.ledger ?? null}
        />
        <SettlementPanel
          obligations={model.snapshot?.settlements.obligations ?? []}
          loading={loading}
        />
      </main>

      {bookOpen ? (
        <BookTradeDialog
          busy={model.writePending}
          onClose={() => setBookOpen(false)}
          onSubmit={model.execute}
        />
      ) : null}
      {amendOpen && selectedTrade?.state === "CONFIRMED" ? (
        <AmendTradeDialog
          trade={selectedTrade}
          busy={model.writePending}
          onClose={() => setAmendOpen(false)}
          onSubmit={model.execute}
        />
      ) : null}
    </div>
  );
}
