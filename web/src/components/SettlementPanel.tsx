import type { SettlementObligation } from "../contracts";
import { MoneyAmount } from "./MoneyAmount";

interface SettlementPanelProps {
  obligations: readonly SettlementObligation[];
  loading: boolean;
}

export function SettlementPanel({
  obligations,
  loading,
}: SettlementPanelProps) {
  return (
    <section
      className="panel settlement-panel"
      aria-labelledby="settlements-title"
    >
      <div className="panel-header">
        <div>
          <p className="eyebrow">Bilateral netting</p>
          <h2 id="settlements-title">Settlement obligations</h2>
        </div>
        <span className="record-count">
          {loading ? "—" : `${obligations.length} open`}
        </span>
      </div>
      {loading ? (
        <div className="panel-skeleton">
          <span className="skeleton-line" />
          <span className="skeleton-line" />
        </div>
      ) : obligations.length === 0 ? (
        <p className="empty-copy">No non-zero settled obligations.</p>
      ) : (
        <div className="table-scroll compact-scroll">
          <table className="data-table settlement-table">
            <thead>
              <tr>
                <th scope="col">Value date</th>
                <th scope="col">Counterparty</th>
                <th scope="col">Netting set</th>
                <th scope="col">Direction</th>
                <th scope="col" className="align-right">
                  Amount
                </th>
              </tr>
            </thead>
            <tbody>
              {obligations.map((obligation) => (
                <tr
                  key={`${obligation.valueDate}:${obligation.amount.currency}:${obligation.counterpartyId}:${obligation.nettingSetId}:${obligation.direction}`}
                >
                  <td>{obligation.valueDate}</td>
                  <td>{obligation.counterpartyId}</td>
                  <td>{obligation.nettingSetId}</td>
                  <td>
                    <span
                      className={`direction direction-${obligation.direction.toLowerCase()}`}
                    >
                      {obligation.direction}
                    </span>
                  </td>
                  <td className="align-right">
                    <MoneyAmount money={obligation.amount} />
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </section>
  );
}
