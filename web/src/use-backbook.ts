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
import { startVisibilityAwarePolling } from "./polling";

export interface SnapshotLoadSuccess {
  ok: true;
  value: DashboardSnapshot;
}

export interface SnapshotLoadFailure {
  ok: false;
  error: ClientError;
  mismatch: boolean;
}

export type SnapshotLoadResult = SnapshotLoadSuccess | SnapshotLoadFailure;

export function snapshotAfterLoad(
  current: DashboardSnapshot | null,
  loaded: SnapshotLoadResult,
): DashboardSnapshot | null {
  return loaded.ok ? loaded.value : current;
}

async function loadSnapshot(): Promise<SnapshotLoadResult> {
  // All three reads must describe the same immutable service snapshot before
  // the dashboard replaces its visible state.
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

  const refreshOnce = useCallback(async (): Promise<boolean> => {
    const sequence = ++requestSequence.current;
    setConnection(snapshotRef.current === null ? "CONNECTING" : "REFRESHING");
    let loaded = await loadSnapshot();
    if (!loaded.ok && loaded.mismatch) {
      // A writer may commit between reads, so one immediate retry is expected.
      loaded = await loadSnapshot();
    }
    if (sequence !== requestSequence.current) {
      return false;
    }
    if (loaded.ok) {
      const nextSnapshot = snapshotAfterLoad(snapshotRef.current, loaded);
      setSnapshot(nextSnapshot);
      snapshotRef.current = nextSnapshot;
      setConnection("LIVE");
      setError(null);
      return true;
    }
    // Failed refreshes retain the last coherent data and only change status.
    setError(loaded.error);
    setConnection(
      loaded.error.kind === "transport" ? "OFFLINE" : "STALE",
    );
    return false;
  }, []);

  const refresh = useCallback(async (): Promise<void> => {
    await refreshOnce();
  }, [refreshOnce]);

  useEffect(() => {
    return startVisibilityAwarePolling(refreshOnce, {
      isVisible: () => document.visibilityState !== "hidden",
      setTimer: (callback, delayMilliseconds) =>
        window.setTimeout(callback, delayMilliseconds),
      clearTimer: (handle) => window.clearTimeout(handle),
      subscribeToVisibility: (listener) => {
        document.addEventListener("visibilitychange", listener);
        return () => document.removeEventListener("visibilitychange", listener);
      },
    });
  }, [refreshOnce]);

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
