import { describe, expect, it } from "vitest";

import { prepareCommandSubmission } from "./commands";
import type { CommandEnvelope } from "./contracts";
import { decodeCommandId, decodeIsoDate } from "./decode";

describe("command submission retry", () => {
  it("reuses the exact envelope until the logical request changes", () => {
    let sequence = 0;
    const create = (): CommandEnvelope => {
      sequence += 1;
      return {
        commandId: decodeCommandId(`CMD-${sequence}`),
        type: "RUN_EOD",
        payload: { asOfDate: decodeIsoDate("2026-07-27") },
      };
    };

    const first = prepareCommandSubmission(null, "same-request", create);
    const retry = prepareCommandSubmission(first, "same-request", create);
    const changed = prepareCommandSubmission(first, "changed-request", create);

    expect(retry).toBe(first);
    expect(retry.command).toBe(first.command);
    expect(JSON.stringify(retry.command)).toBe(JSON.stringify(first.command));
    expect(changed).not.toBe(first);
    expect(changed.command.commandId).not.toBe(first.command.commandId);
  });
});
