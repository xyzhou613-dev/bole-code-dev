/**
 * Delta coalescing: interval, byte bound, revision ordering, and flush on end.
 */

import { afterEach, describe, expect, it, vi } from 'vitest'
import { DeltaCoalescer } from '../src/events/deltas.ts'

function start(attemptId = 'a-1', revision = 1) {
  return { type: 'start', attemptId, revision, turn: 1, step: 1 }
}

function chunk(attemptId = 'a-1', revision = 1, type = 'text-delta', text = 'hi') {
  return { type: 'chunk', attemptId, revision, index: 0, time: 1, chunk: { type, index: 0, text } }
}

afterEach(() => {
  vi.useRealTimers()
})

describe('DeltaCoalescer', () => {
  it('flushes on the interval', () => {
    vi.useFakeTimers()
    const emitted = []
    const coalescer = new DeltaCoalescer((delta) => emitted.push(delta), { intervalMs: 50, maxBytes: 1024 })
    coalescer.push('s-1', start())
    coalescer.push('s-1', chunk('a-1', 1, 'text-delta', 'hel'))
    expect(emitted).toHaveLength(0)
    vi.advanceTimersByTime(50)
    expect(emitted).toEqual([{
      sessionId: 's-1',
      turn: 1,
      step: 1,
      channel: 'text',
      attemptId: 'a-1',
      revision: 1,
      text: 'hel',
    }])
  })

  it('flushes immediately at the byte bound', () => {
    vi.useFakeTimers()
    const emitted = []
    const coalescer = new DeltaCoalescer((delta) => emitted.push(delta), { intervalMs: 50, maxBytes: 4 })
    coalescer.push('s-1', start())
    coalescer.push('s-1', chunk('a-1', 1, 'text-delta', 'abc'))
    expect(emitted).toHaveLength(0)
    coalescer.push('s-1', chunk('a-1', 1, 'text-delta', 'de'))
    expect(emitted).toHaveLength(1)
    expect(emitted[0].text).toBe('abcde')
  })

  it('drops older revisions and separates text from reasoning', () => {
    vi.useFakeTimers()
    const emitted = []
    const coalescer = new DeltaCoalescer((delta) => emitted.push(delta), { intervalMs: 50, maxBytes: 1024 })
    coalescer.push('s-1', start('a-1', 2))
    coalescer.push('s-1', chunk('a-1', 1, 'text-delta', 'old'))
    coalescer.push('s-1', chunk('a-1', 2, 'text-delta', 'T'))
    coalescer.push('s-1', chunk('a-1', 2, 'reasoning-delta', 'R'))
    vi.advanceTimersByTime(50)
    expect(emitted.map((delta) => [delta.channel, delta.text])).toEqual([['text', 'T'], ['reasoning', 'R']])
  })

  it('flushes and forgets an attempt on end', () => {
    vi.useFakeTimers()
    const emitted = []
    const coalescer = new DeltaCoalescer((delta) => emitted.push(delta), { intervalMs: 50, maxBytes: 1024 })
    coalescer.push('s-1', start())
    coalescer.push('s-1', chunk('a-1', 1, 'text-delta', 'tail'))
    coalescer.push('s-1', { type: 'end', attemptId: 'a-1', revision: 1, index: 0, outcome: { kind: 'abandoned' } })
    expect(emitted).toHaveLength(1)
    expect(emitted[0].text).toBe('tail')
    coalescer.push('s-1', chunk('a-1', 1, 'text-delta', 'late'))
    vi.advanceTimersByTime(50)
    expect(emitted).toHaveLength(1)
  })
})
