import type { Money } from "../contracts";
import { moneyParts } from "../money";

interface MoneyAmountProps {
  money: Money;
}

export function MoneyAmount({ money }: MoneyAmountProps) {
  const parts = moneyParts(money);
  return (
    <span className="money-value">
      {parts.negative ? <span aria-hidden="true">(</span> : null}
      <span className="money-integer">{parts.integer}</span>
      {parts.fraction.length > 0 ? (
        <>
          <span className="money-decimal">.</span>
          <span className="money-fraction">{parts.fraction}</span>
        </>
      ) : null}
      <span className="money-currency">{parts.currency}</span>
      {parts.negative ? <span aria-hidden="true">)</span> : null}
    </span>
  );
}
