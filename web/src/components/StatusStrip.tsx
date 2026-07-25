import type { ConnectionStatus, IsoDate, Money } from "../contracts";
import { MoneyAmount } from "./MoneyAmount";

interface StatusStripProps {
  totals: readonly Money[];
  balanced: boolean;
  ready: boolean;
  connection: ConnectionStatus;
  writesDisabled: boolean;
  writePending: boolean;
  canAmend: boolean;
  eodDate: IsoDate | null;
  onRefresh: () => void;
  onBook: () => void;
  onAmend: () => void;
  onEod: () => void;
}

export function StatusStrip({
  totals,
  balanced,
  ready,
  connection,
  writesDisabled,
  writePending,
  canAmend,
  eodDate,
  onRefresh,
  onBook,
  onAmend,
  onEod,
}: StatusStripProps) {
  return (
    <div className="status-strip" aria-live="polite">
      <div
        className={`balance-summary ${
          ready && balanced ? "is-balanced" : ready ? "is-alert" : ""
        }`}
      >
        <span className="balance-label">
          {!ready ? "CHECKING" : balanced ? "BALANCED" : "OUT OF BALANCE"}
        </span>
        {totals.map((total) => (
          <MoneyAmount key={total.currency} money={total} />
        ))}
      </div>
      <div className="status-actions">
        <button
          className="button button-secondary"
          type="button"
          onClick={onRefresh}
          disabled={connection === "CONNECTING" || connection === "REFRESHING"}
        >
          {connection === "REFRESHING" ? "Refreshing…" : "Refresh"}
        </button>
        <button
          className="button"
          type="button"
          onClick={onBook}
          disabled={writesDisabled}
        >
          Book trade
        </button>
        <button
          className="button"
          type="button"
          onClick={onAmend}
          disabled={writesDisabled || !canAmend}
        >
          Amend
        </button>
        <button
          className="button"
          type="button"
          onClick={onEod}
          disabled={writesDisabled || eodDate === null}
          title={
            eodDate === null ? "No confirmed trade is eligible" : `Run ${eodDate}`
          }
        >
          {writePending ? "Committing…" : "Run EOD"}
        </button>
      </div>
    </div>
  );
}
