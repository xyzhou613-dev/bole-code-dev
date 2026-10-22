/**
 * Coalesce transient assistant-stream chunks into `assistant.delta`
 * notifications (02-protocol.md §3, 03-events.md §5).
 *
 * Chunks are merged per attempt and channel with a 50 ms / 4 KB bound. Both
 * bounds are best-effort: the final text is authoritative in
 * `ASSISTANT_MESSAGE`, so dropping a delta never changes the durable result.
 */

import type { AssistantStreamFrame } from '@deepseek-ai/dsh-agent'

/** Delta channels in 03-events.md. */
export type DeltaChannel = 'text' | 'reasoning'

/** One coalesced delta notification payload. */
export interface AssistantDelta {
  sessionId: string
  turn: number
  step: number
  channel: DeltaChannel
  attemptId: string
  revision: number
  text: string
}

/** Coalescer options; values exist for tests and future deployment tuning. */
export interface DeltaCoalescerOptions {
  /** Maximum delay before a partial buffer is emitted. Default: 50 ms. */
  intervalMs?: number
  /** Maximum buffered UTF-8 bytes per channel. Default: 4 KiB. */
  maxBytes?: number
}

interface ChannelBuffer {
  text: string
  bytes: number
  timer: ReturnType<typeof setTimeout> | undefined
}

interface Attempt {
  attemptId: string
  sessionId: string
  turn: number
  step: number
  revision: number
  readonly channels: Map<DeltaChannel, ChannelBuffer>
}

/**
 * Merge transient chunks and emit bounded assistant deltas.
 */
export class DeltaCoalescer {
  private readonly attempts = new Map<string, Attempt>()
  private readonly intervalMs: number
  private readonly maxBytes: number

  /**
   * @param emit - sink for one coalesced delta.
   * @param options - interval and byte bounds.
   */
  constructor(
    private readonly emit: (delta: AssistantDelta) => void,
    options: DeltaCoalescerOptions = {},
  ) {
    this.intervalMs = options.intervalMs ?? 50
    this.maxBytes = options.maxBytes ?? 4096
  }

  /**
   * Consume one stream frame.
   * @param sessionId - owning session.
   * @param frame - start/chunk/end frame.
   */
  push(sessionId: string, frame: AssistantStreamFrame): void {
    if (frame.type === 'start') {
      this.attempts.set(frame.attemptId, {
        attemptId: frame.attemptId,
        sessionId,
        turn: frame.turn,
        step: frame.step,
        revision: frame.revision,
        channels: new Map(),
      })
      return
    }
    if (frame.type === 'end') {
      this.flushAttempt(frame.attemptId)
      this.attempts.delete(frame.attemptId)
      return
    }
    const attempt = this.attempts.get(frame.attemptId)
    if (attempt === undefined || frame.revision < attempt.revision) return
    const chunk = frame.chunk as { type?: unknown; text?: unknown }
    const channel: DeltaChannel | undefined = chunk.type === 'text-delta'
      ? 'text'
      : chunk.type === 'reasoning-delta'
        ? 'reasoning'
        : undefined
    if (channel === undefined || typeof chunk.text !== 'string' || chunk.text === '') return
    let buffer = attempt.channels.get(channel)
    if (buffer === undefined) {
      buffer = { text: '', bytes: 0, timer: undefined }
      attempt.channels.set(channel, buffer)
    }
    buffer.text += chunk.text
    buffer.bytes += Buffer.byteLength(chunk.text, 'utf8')
    if (buffer.bytes >= this.maxBytes) {
      this.flushChannel(attempt, channel, buffer)
      return
    }
    if (buffer.timer === undefined) {
      buffer.timer = setTimeout(() => {
        buffer.timer = undefined
        this.flushChannel(attempt, channel, buffer)
      }, this.intervalMs)
      buffer.timer.unref?.()
    }
  }

  /**
   * Flush and forget every attempt belonging to one session.
   * @param sessionId - session whose turn ended or whose agent is gone.
   */
  flushSession(sessionId: string): void {
    for (const [attemptId, attempt] of this.attempts) {
      if (attempt.sessionId !== sessionId) continue
      this.flushAttempt(attemptId)
      this.attempts.delete(attemptId)
    }
  }

  /** Flush every buffered attempt; used before shutdown. */
  flushAll(): void {
    for (const attemptId of [...this.attempts.keys()]) this.flushAttempt(attemptId)
    this.attempts.clear()
  }

  private flushAttempt(attemptId: string): void {
    const attempt = this.attempts.get(attemptId)
    if (attempt === undefined) return
    for (const [channel, buffer] of attempt.channels) this.flushChannel(attempt, channel, buffer)
    attempt.channels.clear()
  }

  private flushChannel(attempt: Attempt, channel: DeltaChannel, buffer: ChannelBuffer): void {
    if (buffer.timer !== undefined) {
      clearTimeout(buffer.timer)
      buffer.timer = undefined
    }
    if (buffer.text === '') return
    const text = buffer.text
    buffer.text = ''
    buffer.bytes = 0
    this.emit({
      sessionId: attempt.sessionId,
      turn: attempt.turn,
      step: attempt.step,
      channel,
      attemptId: attempt.attemptId,
      revision: attempt.revision,
      text,
    })
  }

  /** Clear timers without emitting; used when the transport is already closed. */
  dispose(): void {
    for (const attempt of this.attempts.values()) {
      for (const buffer of attempt.channels.values()) {
        if (buffer.timer !== undefined) clearTimeout(buffer.timer)
      }
    }
    this.attempts.clear()
  }
}
