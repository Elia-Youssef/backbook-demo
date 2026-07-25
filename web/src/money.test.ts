import { describe, expect, it } from "vitest";

import { decodeMoney } from "./decode";
import { moneyInputValue, moneyParts, parsePositiveMoney } from "./money";

describe("money presentation", () => {
  it("uses each currency exponent without passing through number", () => {
    expect(
      moneyParts(decodeMoney({ currency: "USD", minorUnits: "10125000" })),
    ).toEqual({
      negative: false,
      integer: "101,250",
      fraction: "00",
      currency: "USD",
    });
    expect(
      moneyParts(decodeMoney({ currency: "JPY", minorUnits: "15187500" })),
    ).toEqual({
      negative: false,
      integer: "15,187,500",
      fraction: "",
      currency: "JPY",
    });
    expect(
      moneyParts(decodeMoney({ currency: "KWD", minorUnits: "35750125" })),
    ).toEqual({
      negative: false,
      integer: "35,750",
      fraction: "125",
      currency: "KWD",
    });
  });

  it("retains accounting sign and exact edit values", () => {
    const money = decodeMoney({ currency: "KWD", minorUnits: "-1250" });
    expect(moneyParts(money).negative).toBe(true);
    expect(moneyInputValue(money)).toBe("-1.250");
  });
});

describe("money input", () => {
  it("converts positive major-unit text to canonical minor-unit strings", () => {
    expect(parsePositiveMoney("101250.00", "USD")?.minorUnits).toBe("10125000");
    expect(parsePositiveMoney("15187500", "JPY")?.minorUnits).toBe("15187500");
    expect(parsePositiveMoney("35750.125", "KWD")?.minorUnits).toBe(
      "35750125",
    );
  });

  it("rejects zero, sign characters, separators, and excess precision", () => {
    expect(parsePositiveMoney("0", "USD")).toBeNull();
    expect(parsePositiveMoney("-1.00", "USD")).toBeNull();
    expect(parsePositiveMoney("+1.00", "USD")).toBeNull();
    expect(parsePositiveMoney("1,000.00", "USD")).toBeNull();
    expect(parsePositiveMoney("1.001", "USD")).toBeNull();
    expect(parsePositiveMoney("1.0", "JPY")).toBeNull();
  });
});
