import type { Trade } from "../contracts";
import { MoneyAmount } from "./MoneyAmount";

export function tradeKey(trade: Trade): string {
  return `${trade.tradeId}:${trade.version}`;
}

interface BlotterTableProps {
  trades: readonly Trade[];
  selectedKey: string | null;
  loading: boolean;
  onSelect: (trade: Trade) => void;
  onBook: () => void;
}

function SkeletonRows() {
  return (
    <>
      {Array.from({ length: 7 }, (_, index) => (
        <tr className="skeleton-row" key={index}>
          <td colSpan={9}>
            <span className="skeleton-line" />
          </td>
        </tr>
      ))}
    </>
  );
}

export function BlotterTable({
  trades,
  selectedKey,
  loading,
  onSelect,
  onBook,
}: BlotterTableProps) {
  return (
    <section className="panel blotter-panel" aria-labelledby="blotter-title">
      <div className="panel-header">
        <div>
          <p className="eyebrow">Lifecycle register</p>
          <h2 id="blotter-title">Trade blotter</h2>
        </div>
        <span className="record-count">
          {loading ? "—" : `${trades.length} versions`}
        </span>
      </div>
      <div className="table-scroll" aria-busy={loading}>
        <table className="data-table blotter-table">
          <thead>
            <tr>
              <th scope="col">Trade / v</th>
              <th scope="col">State</th>
              <th scope="col">Kind</th>
              <th scope="col">Counterparty</th>
              <th scope="col" className="align-right">
                Pay
              </th>
              <th scope="col" className="align-right">
                Receive
              </th>
              <th scope="col">Value date</th>
              <th scope="col">Book</th>
              <th scope="col">Successor</th>
            </tr>
          </thead>
          <tbody>
            {loading ? <SkeletonRows /> : null}
            {!loading && trades.length === 0 ? (
              <tr>
                <td colSpan={9}>
                  <div className="empty-state">
                    <strong>No trades booked</strong>
                    <span>Capture the first synthetic FX trade.</span>
                    <button className="button" type="button" onClick={onBook}>
                      Book trade
                    </button>
                  </div>
                </td>
              </tr>
            ) : null}
            {!loading
              ? trades.map((trade) => {
                  const key = tradeKey(trade);
                  return (
                    <tr
                      key={key}
                      className={selectedKey === key ? "is-selected" : undefined}
                      aria-selected={selectedKey === key}
                      tabIndex={0}
                      onClick={() => onSelect(trade)}
                      onKeyDown={(event) => {
                        if (event.key === "Enter" || event.key === " ") {
                          event.preventDefault();
                          onSelect(trade);
                        }
                      }}
                    >
                      <td>
                        <span className="primary-cell">{trade.tradeId}</span>
                        <span className="subtle-inline">v{trade.version}</span>
                      </td>
                      <td>
                        <span className={`state state-${trade.state.toLowerCase()}`}>
                          {trade.state}
                        </span>
                      </td>
                      <td>{trade.terms.kind.replace("FX_", "")}</td>
                      <td>{trade.counterpartyId}</td>
                      <td className="align-right">
                        <MoneyAmount money={trade.terms.pay} />
                      </td>
                      <td className="align-right">
                        <MoneyAmount money={trade.terms.receive} />
                      </td>
                      <td>{trade.terms.valueDate}</td>
                      <td>{trade.bookId}</td>
                      <td>
                        {trade.supersededBy === null
                          ? "—"
                          : `v${trade.supersededBy}`}
                      </td>
                    </tr>
                  );
                })
              : null}
          </tbody>
        </table>
      </div>
    </section>
  );
}
