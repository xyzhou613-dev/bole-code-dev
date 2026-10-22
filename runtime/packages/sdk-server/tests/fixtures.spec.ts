/**
 * Fixture invariants shared with the C contract tests: envelope shape,
 * monotonic event ids, one terminal event, and manifest coverage.
 */

import { readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'

const fixtureDir = fileURLToPath(new URL('../../../../native/tests/fixtures/', import.meta.url))
const manifest = JSON.parse(await readFile(`${fixtureDir}manifest.json`, 'utf8'))

describe('fixtures', () => {
  for (const entry of manifest.fixtures) {
    it(`${entry.file} keeps the wire invariants`, async () => {
      const lines = (await readFile(`${fixtureDir}${entry.file}`, 'utf8')).split('\n').filter(Boolean)
      const frames = lines.map((line) => JSON.parse(line))
      let lastEventId = 0
      const types = []
      for (const frame of frames) {
        expect(['bole.event', 'assistant.delta', 'rsi.state']).toContain(frame.method)
        expect(frame.jsonrpc).toBe('2.0')
        if (frame.method === 'bole.event') {
          const event = frame.params.event
          expect(typeof event.event_id).toBe('number')
          expect(event.event_id).toBeGreaterThan(lastEventId)
          lastEventId = event.event_id
          expect(typeof event.type).toBe('number')
          expect(typeof event.session_id).toBe('string')
          expect(typeof event.task).toBe('number')
          expect(typeof event.step).toBe('number')
          expect(typeof event.seq).toBe('number')
          expect(typeof event.ts_ms).toBe('number')
          expect(event.payload.v).toBe(1)
          types.push(event.type)
        } else if (frame.method === 'assistant.delta') {
          expect(frame.params.sessionId).toBe('s-001')
          expect(['text', 'reasoning']).toContain(frame.params.channel)
          expect(typeof frame.params.text).toBe('string')
        }
      }
      for (const type of entry.expectedEventTypes) expect(types).toContain(type)
      const terminals = types.filter((type) => type === 50 || type === 51)
      expect(terminals).toHaveLength(1)
      if (entry.terminal === 'aborted') {
        const finished = frames.find((frame) => frame.method === 'bole.event' && frame.params.event.type === 50)
        expect(finished.params.event.payload.outcome).toBe('aborted')
      }
      if (entry.terminal === 'failed') {
        expect(terminals).toEqual([51])
      }
    })
  }
})
