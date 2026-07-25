import type { ClientError } from "../contracts";
import { MoneyAmount } from "./MoneyAmount";

interface ProblemPanelProps {
  error: ClientError;
  onDismiss: () => void;
}

export function ProblemPanel({ error, onDismiss }: ProblemPanelProps) {
  if (error.kind === "problem") {
    const problem = error.problem;
    return (
      <aside className="problem-panel" aria-live="assertive">
        <div>
          <span className="problem-code">{problem.code}</span>
          <strong>{problem.title}</strong>
          <p>{problem.detail}</p>
          {problem.nodePath === undefined ? null : (
            <p className="problem-detail">
              Limit path: {problem.nodePath.join(" / ")}
            </p>
          )}
          {problem.required === undefined ||
          problem.remaining === undefined ? null : (
            <p className="problem-detail">
              Required <MoneyAmount money={problem.required} /> · Remaining{" "}
              <MoneyAmount money={problem.remaining} />
            </p>
          )}
          {problem.violations === undefined ? null : (
            <ul className="violation-list">
              {problem.violations.map((violation) => (
                <li key={`${violation.field}:${violation.message}`}>
                  <code>{violation.field}</code> {violation.message}
                </li>
              ))}
            </ul>
          )}
        </div>
        <button
          className="dismiss-button"
          type="button"
          onClick={onDismiss}
          aria-label="Dismiss problem"
        >
          Dismiss
        </button>
      </aside>
    );
  }

  const title =
    error.kind === "transport" ? "Service unavailable" : "Protocol mismatch";
  return (
    <aside className="problem-panel" aria-live="assertive">
      <div>
        <span className="problem-code">
          {error.kind === "transport" ? error.reason.toUpperCase() : "PROTOCOL"}
        </span>
        <strong>{title}</strong>
        <p>{error.message}</p>
      </div>
      <button
        className="dismiss-button"
        type="button"
        onClick={onDismiss}
        aria-label="Dismiss problem"
      >
        Dismiss
      </button>
    </aside>
  );
}
