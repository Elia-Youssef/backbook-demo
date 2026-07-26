import type {
  ClientError,
  ClientResult,
  CommandEnvelope,
  CommandResponse,
  LedgerData,
  ReadEnvelope,
  SettlementData,
  StateData,
} from "./contracts";
import {
  decodeCommandResponse,
  decodeLedgerResponse,
  decodeProblem,
  decodeSettlementsResponse,
  decodeStateResponse,
} from "./decode";

const requestTimeoutMilliseconds = 5_000;

type Decoder<Value> = (source: unknown) => Value;

function protocolError(message: string): ClientError {
  return { kind: "protocol", message };
}

async function request<Value>(
  path: string,
  decoder: Decoder<Value>,
  init?: RequestInit,
): Promise<ClientResult<Value>> {
  const controller = new AbortController();
  const timeout = globalThis.setTimeout(
    () => controller.abort(),
    requestTimeoutMilliseconds,
  );
  try {
    const response = await fetch(path, { ...init, signal: controller.signal });
    let body: unknown;
    try {
      body = await response.json();
    } catch {
      return {
        ok: false,
        status: response.status,
        error: protocolError("The service returned a non-JSON response."),
      };
    }

    if (!response.ok) {
      try {
        return {
          ok: false,
          status: response.status,
          error: { kind: "problem", problem: decodeProblem(body) },
        };
      } catch {
        return {
          ok: false,
          status: response.status,
          error: protocolError(
            "The service returned an invalid problem response.",
          ),
        };
      }
    }

    try {
      return { ok: true, status: response.status, value: decoder(body) };
    } catch {
      return {
        ok: false,
        status: response.status,
        error: protocolError("The service response failed validation."),
      };
    }
  } catch (error: unknown) {
    const timedOut =
      error instanceof DOMException && error.name === "AbortError";
    return {
      ok: false,
      error: {
        kind: "transport",
        reason: timedOut ? "timeout" : "network",
        message: timedOut
          ? "The service did not respond before the timeout."
          : "The service is unreachable.",
      },
    };
  } finally {
    globalThis.clearTimeout(timeout);
  }
}

export function readState(): Promise<ClientResult<ReadEnvelope<StateData>>> {
  return request("/api/v1/state", decodeStateResponse);
}

export function readLedger(): Promise<ClientResult<ReadEnvelope<LedgerData>>> {
  return request("/api/v1/ledger", decodeLedgerResponse);
}

export function readSettlements(): Promise<
  ClientResult<ReadEnvelope<SettlementData>>
> {
  return request("/api/v1/settlements", decodeSettlementsResponse);
}

export function postCommand(
  command: CommandEnvelope,
): Promise<ClientResult<CommandResponse>> {
  return request("/api/v1/commands", decodeCommandResponse, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(command),
  });
}
