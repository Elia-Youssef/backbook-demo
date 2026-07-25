import type { LedgerData, Posting, Trade } from "../contracts";
import { MoneyAmount } from "./MoneyAmount";

interface LedgerPanelProps {
  trade: Trade;
  ledger: LedgerData;
}

function selectedPostings(ledger: LedgerData, trade: Trade): readonly Posting[] {
  return ledger.entries
    .flatMap((entry) => entry.postings)
    .filter(
      (posting) =>
        posting.tradeId === trade.tradeId &&
        posting.tradeVersion === trade.version,
    );
}

export function LedgerPanel({ trade, ledger }: LedgerPanelProps) {
  const postings = selectedPostings(ledger, trade);
  return (
    <div className="ledger-section">
      <div className="section-heading">
        <h3>T-account postings</h3>
        <span>{postings.length} lines</span>
      </div>
      {postings.length === 0 ? (
        <p className="empty-copy">No ledger entry exists for this version.</p>
      ) : (
        <div className="table-scroll compact-scroll">
          <table className="data-table ledger-table">
            <thead>
              <tr>
                <th scope="col">Account</th>
                <th scope="col" className="align-right">
                  Debit
                </th>
                <th scope="col" className="align-right">
                  Credit
                </th>
                <th scope="col">Link</th>
              </tr>
            </thead>
            <tbody>
              {postings.map((posting) => (
                <tr key={posting.postingId}>
                  <td title={posting.account}>{posting.account}</td>
                  <td className="align-right">
                    {posting.side === "DEBIT" ? (
                      <MoneyAmount money={posting.amount} />
                    ) : (
                      "—"
                    )}
                  </td>
                  <td className="align-right">
                    {posting.side === "CREDIT" ? (
                      <MoneyAmount money={posting.amount} />
                    ) : (
                      "—"
                    )}
                  </td>
                  <td className="link-cell">
                    {posting.reversalOf === null ? "ORIGINAL" : "REVERSAL"}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
}
