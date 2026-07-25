import type { Currency, DecimalString, Money, MoneyDto } from "./contracts";
import { decodeDecimalString } from "./decode";

const exponents: Record<Currency, number> = {
  USD: 2,
  JPY: 0,
  KWD: 3,
};

export interface MoneyParts {
  negative: boolean;
  integer: string;
  fraction: string;
  currency: Currency;
}

export function moneyParts(money: Money): MoneyParts {
  const exponent = exponents[money.currency];
  const negative = money.minorUnits < 0n;
  const absolute = negative ? -money.minorUnits : money.minorUnits;
  const scale = 10n ** BigInt(exponent);
  const integer = (absolute / scale).toLocaleString("en-US");
  const fraction =
    exponent === 0
      ? ""
      : (absolute % scale).toString().padStart(exponent, "0");
  return { negative, integer, fraction, currency: money.currency };
}

export function moneyInputValue(money: Money): string {
  const parts = moneyParts(money);
  const unsigned =
    parts.fraction.length === 0
      ? parts.integer.replaceAll(",", "")
      : `${parts.integer.replaceAll(",", "")}.${parts.fraction}`;
  return parts.negative ? `-${unsigned}` : unsigned;
}

export function parsePositiveMoney(
  text: string,
  currency: Currency,
): MoneyDto | null {
  const exponent = exponents[currency];
  const match = /^(0|[1-9][0-9]*)(?:\.([0-9]+))?$/.exec(text);
  if (match === null) {
    return null;
  }
  const whole = match[1];
  const fraction = match[2] ?? "";
  if (whole === undefined || fraction.length > exponent) {
    return null;
  }
  if (exponent === 0 && fraction.length !== 0) {
    return null;
  }
  const paddedFraction = fraction.padEnd(exponent, "0");
  const combined = `${whole}${paddedFraction}`.replace(/^0+(?=[0-9])/, "");
  const canonical = combined.length === 0 ? "0" : combined;
  if (BigInt(canonical) <= 0n) {
    return null;
  }
  return {
    currency,
    minorUnits: decodeDecimalString(canonical),
  };
}

export function decimalString(value: bigint): DecimalString {
  return decodeDecimalString(value.toString());
}
