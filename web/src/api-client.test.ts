import { afterEach, describe, expect, it, vi } from "vitest";

import { readState } from "./api-client";

afterEach(() => {
  vi.unstubAllGlobals();
});

describe("API transport boundary", () => {
  it("returns a transport result when fetch rejects", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn(async () => {
        throw new TypeError("connection refused");
      }),
    );

    const result = await readState();

    expect(result.ok).toBe(false);
    if (!result.ok) {
      expect(result.error.kind).toBe("transport");
      if (result.error.kind === "transport") {
        expect(result.error.reason).toBe("network");
      }
    }
  });
});
