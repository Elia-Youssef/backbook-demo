const basePollingDelayMilliseconds = 5_000;
const maximumPollingDelayMilliseconds = 60_000;

export interface PollingEnvironment {
  isVisible: () => boolean;
  setTimer: (callback: () => void, delayMilliseconds: number) => number;
  clearTimer: (handle: number) => void;
  subscribeToVisibility: (listener: () => void) => () => void;
}

export function pollingDelayMilliseconds(
  consecutiveFailures: number,
): number {
  const exponent = Math.min(Math.max(consecutiveFailures, 0), 4);
  return Math.min(
    basePollingDelayMilliseconds * 2 ** exponent,
    maximumPollingDelayMilliseconds,
  );
}

export function startVisibilityAwarePolling(
  refresh: () => Promise<boolean>,
  environment: PollingEnvironment,
): () => void {
  let stopped = false;
  let refreshInFlight = false;
  let timer: number | null = null;
  let consecutiveFailures = 0;

  function clearTimer() {
    if (timer !== null) {
      environment.clearTimer(timer);
      timer = null;
    }
  }

  function schedule() {
    clearTimer();
    if (stopped || !environment.isVisible()) {
      return;
    }
    timer = environment.setTimer(() => {
      timer = null;
      void runRefresh();
    }, pollingDelayMilliseconds(consecutiveFailures));
  }

  async function runRefresh() {
    if (stopped || refreshInFlight || !environment.isVisible()) {
      return;
    }
    refreshInFlight = true;
    let succeeded = false;
    try {
      succeeded = await refresh();
    } catch {
      succeeded = false;
    }
    refreshInFlight = false;
    if (stopped) {
      return;
    }
    consecutiveFailures = succeeded ? 0 : consecutiveFailures + 1;
    schedule();
  }

  const unsubscribe = environment.subscribeToVisibility(() => {
    clearTimer();
    if (environment.isVisible()) {
      void runRefresh();
    }
  });
  void runRefresh();

  return () => {
    stopped = true;
    clearTimer();
    unsubscribe();
  };
}
