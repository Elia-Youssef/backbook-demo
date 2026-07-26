import { describe, expect, it, vi } from "vitest";

import { startVisibilityAwarePolling } from "./polling";
import type { PollingEnvironment } from "./polling";

class FakePollingEnvironment implements PollingEnvironment {
  visible = true;
  nextHandle = 1;
  timers = new Map<number, { callback: () => void; delay: number }>();
  listeners = new Set<() => void>();

  isVisible = () => this.visible;

  setTimer = (callback: () => void, delay: number) => {
    const handle = this.nextHandle++;
    this.timers.set(handle, { callback, delay });
    return handle;
  };

  clearTimer = (handle: number) => {
    this.timers.delete(handle);
  };

  subscribeToVisibility = (listener: () => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };

  setVisible(visible: boolean) {
    this.visible = visible;
    for (const listener of this.listeners) {
      listener();
    }
  }

  fireNextTimer() {
    const next = this.timers.entries().next().value;
    if (next === undefined) {
      throw new Error("No timer is scheduled.");
    }
    const [handle, timer] = next;
    this.timers.delete(handle);
    timer.callback();
  }

  scheduledDelays() {
    return [...this.timers.values()].map((timer) => timer.delay);
  }
}

async function flushPromises() {
  await Promise.resolve();
  await Promise.resolve();
}

describe("visibility-aware polling", () => {
  it("polls every five seconds and pauses while hidden", async () => {
    const environment = new FakePollingEnvironment();
    const refresh = vi.fn(async () => true);

    const stop = startVisibilityAwarePolling(refresh, environment);
    await flushPromises();
    expect(refresh).toHaveBeenCalledTimes(1);
    expect(environment.scheduledDelays()).toEqual([5_000]);

    environment.setVisible(false);
    expect(environment.scheduledDelays()).toEqual([]);
    environment.setVisible(true);
    await flushPromises();
    expect(refresh).toHaveBeenCalledTimes(2);
    expect(environment.scheduledDelays()).toEqual([5_000]);

    stop();
    expect(environment.scheduledDelays()).toEqual([]);
  });

  it("backs off after a failed read and resets after success", async () => {
    const environment = new FakePollingEnvironment();
    const outcomes = [false, true];
    const refresh = vi.fn(async () => outcomes.shift() ?? true);

    const stop = startVisibilityAwarePolling(refresh, environment);
    await flushPromises();
    expect(environment.scheduledDelays()).toEqual([10_000]);

    environment.fireNextTimer();
    await flushPromises();
    expect(environment.scheduledDelays()).toEqual([5_000]);

    stop();
  });
});
