import type { LedgerData, Trade } from "../contracts";
import { LedgerPanel } from "./LedgerPanel";
import { MoneyAmount } from "./MoneyAmount";

interface TradeInspectorProps {
  trade: Trade | null;
  ledger: LedgerData | null;
}

export function TradeInspector({ trade, ledger }: TradeInspectorProps) {
  return (
    <section className="panel inspector-panel" aria-labelledby="inspector-title">
      <div className="panel-header">
        <div>
          <p className="eyebrow">Version evidence</p>
          <h2 id="inspector-title">Trade inspector</h2>
        </div>
        {trade === null ? null : (
          <span className={`state state-${trade.state.toLowerCase()}`}>
            {trade.state}
          </span>
        )}
      </div>
      {trade === null || ledger === null ? (
        <div className="empty-state compact-empty">
          <strong>Select a trade version</strong>
          <span>Lifecycle links and postings will appear here.</span>
        </div>
      ) : (
        <>
          <dl className="trade-summary">
            <div>
              <dt>Trade</dt>
              <dd>
                {trade.tradeId} · v{trade.version}
              </dd>
            </div>
            <div>
              <dt>Contract</dt>
              <dd>{trade.terms.kind.replace("FX_", "FX ")}</dd>
            </div>
            <div>
              <dt>Path</dt>
              <dd>
                {trade.counterpartyId} / {trade.nettingSetId} / {trade.bookId}
              </dd>
            </div>
            <div>
              <dt>Dates</dt>
              <dd>
                {trade.terms.tradeDate} → {trade.terms.valueDate}
              </dd>
            </div>
            <div>
              <dt>Pay</dt>
              <dd>
                <MoneyAmount money={trade.terms.pay} />
              </dd>
            </div>
            <div>
              <dt>Receive</dt>
              <dd>
                <MoneyAmount money={trade.terms.receive} />
              </dd>
            </div>
            <div>
              <dt>Supersedes</dt>
              <dd>
                {trade.supersedes === null ? "—" : `v${trade.supersedes}`}
              </dd>
            </div>
            <div>
              <dt>Successor</dt>
              <dd>
                {trade.supersededBy === null
                  ? "—"
                  : `v${trade.supersededBy}`}
              </dd>
            </div>
          </dl>
          <LedgerPanel trade={trade} ledger={ledger} />
        </>
      )}
    </section>
  );
}
