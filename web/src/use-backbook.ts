import { useCallback, useEffect, useRef, useState } from "react";

import {
  postCommand,
  readLedger,
  readSettlements,
  readState,
} from "./api-client";
import type {
  ClientError,
  CommandEnvelope,
  ConnectionStatus,
  DashboardSnapshot,
} from "./contracts";
import { combineSnapshots, SnapshotMismatchError } from "./decode";

interface SnapshotLoadSuccess {
  ok: true;
  value: DashboardSnapshot;
}

interface SnapshotLoadFailure {
  ok: false;
  error: ClientError;
  mismatch: boolean;
}

type SnapshotLoadResult = SnapshotLoadSuccess | SnapshotLoadFailure;

async function loadSnapshot(): Promise<SnapshotLoadResult> {
  const [state, ledger, settlements] = await Promise.all([
    readState(),
    readLedger(),
    readSettlements(),
  ]);
  if (!state.ok) {
    return { ok: false, error: state.error, mismatch: false };
  }
  if (!ledger.ok) {
    return { ok: false, error: ledger.error, mismatch: false };
  }
  if (!settlements.ok) {
    return { ok: false, error: settlements.error, mismatch: false };
  }
  try {
    return {
      ok: true,
      value: combineSnapshots(state.value, ledger.value, settlements.value),
    };
  } catch (error: unknown) {
    return {
      ok: false,
      mismatch: error instanceof SnapshotMismatchError,
      error: {
        kind: "protocol",
        message:
          error instanceof SnapshotMismatchError
            ? error.message
            : "The service snapshot could not be assembled.",
      },
    };
  }
}

export interface BackbookModel {
  snapshot: DashboardSnapshot | null;
  connection: ConnectionStatus;
  error: ClientError | null;
  writePending: boolean;
  writesDisabled: boolean;
  refresh: () => Promise<void>;
  execute: (command: CommandEnvelope) => Promise<boolean>;
  clearError: () => void;
}

export function useBackbook(): BackbookModel {
  const [snapshot, setSnapshot] = useState<DashboardSnapshot | null>(null);
  const [connection, setConnection] =
    useState<ConnectionStatus>("CONNECTING");
  const [error, setError] = useState<ClientError | null>(null);
  const [writePending, setWritePending] = useState(false);
  const snapshotRef = useRef<DashboardSnapshot | null>(null);
  const requestSequence = useRef(0);
  const writeInFlight = useRef(false);

  useEffect(() => {
    snapshotRef.current = snapshot;
  }, [snapshot]);

  const refresh = useCallback(async (): Promise<void> => {
    const sequence = ++requestSequence.current;
    setConnection(snapshotRef.current === null ? "CONNECTING" : "REFRESHING");
    let loaded = await loadSnapshot();
    if (!loaded.ok && loaded.mismatch) {
      loaded = await loadSnapshot();
    }
    if (sequence !== requestSequence.current) {
      return;
    }
    if (loaded.ok) {
      setSnapshot(loaded.value);
      snapshotRef.current = loaded.value;
      setConnection("LIVE");
      setError(null);
      return;
    }
    setError(loaded.error);
    setConnection(
      loaded.error.kind === "transport" ? "OFFLINE" : "STALE",
    );
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const execute = useCallback(
    async (command: CommandEnvelope): Promise<boolean> => {
      if (connection !== "LIVE" || writeInFlight.current) {
        return false;
      }
      writeInFlight.current = true;
      setWritePending(true);
      const result = await postCommand(command);
      if (!result.ok) {
        setError(result.error);
        if (result.error.kind === "transport") {
          setConnection("OFFLINE");
        }
        setWritePending(false);
        writeInFlight.current = false;
        return false;
      }
      await refresh();
      setWritePending(false);
      writeInFlight.current = false;
      return true;
    },
    [connection, refresh],
  );

  const clearError = useCallback(() => setError(null), []);

  return {
    snapshot,
    connection,
    error,
    writePending,
    writesDisabled: connection !== "LIVE" || writePending,
    refresh,
    execute,
    clearError,
  };
}
