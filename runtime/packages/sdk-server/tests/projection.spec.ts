/**
 * Projection conformance: one case per SDK event source and the truncation
 * boundaries from 03-events.md.
 */

import { describe, expect, it } from 'vitest'
import { projectSessionEvent, truncateUtf8, type ProjectionState, type RawSessionEvent } from '../src/events/projection.ts'

const options = { sessionId: 's-001', composed: true }

function state(): ProjectionState {
  return { turn: 0, step: 0, preset: undefined, tools: new Map() }
}

function event(type: string, data: Record<string, unknown>, seq = 1, time = 1_700_000_000_000): RawSessionEvent {
  return { type, seq, time, data }
}

describe('projectSessionEvent', () => {
  it('projects turn and step boundaries', () => {
    const current = state()
    expect(projectSessionEvent(event('turn/start', { turn: 1 }), current, options)).toMatchObject({
      type: 10,
      task: 1,
      step: 0,
      payload: { v: 1, turn: 1 },
    })
    expect(projectSessionEvent(event('step/start', { turn: 1, step: 2 }), current, options)).toMatchObject({
      type: 11,
      task: 1,
      step: 2,
      payload: { v: 1, turn: 1, step: 2 },
    })
    expect(projectSessionEvent(event('step/end', { turn: 1, step: 2 }), current, options)).toMatchObject({
      type: 12,
      task: 1,
      step: 2,
    })
  })

  it('projects plan mode and todos as one event type with a kind', () => {
    const current = state()
    expect(projectSessionEvent(event('plan/mode', { active: true }), current, options)?.payload).toMatchObject({
      kind: 'plan_mode',
      active: true,
    })
    expect(projectSessionEvent(event('todo/write', { todos: [{ content: 'a', status: 'pending' }] }), current, options)?.payload).toMatchObject({
      kind: 'todos',
      todos: [{ content: 'a', status: 'pending' }],
    })
  })

  it('projects the request header', () => {
    const current = state()
    current.turn = 2
    current.step = 3
    const projected = projectSessionEvent(event('request/header', {
      header: { config: { provider: 'bole', model: 'qwen3.8-27b', reasoningEffort: 'high', maxTokens: 8192 } },
      reason: 'initial',
    }), current, options)
    expect(projected).toMatchObject({
      type: 14,
      task: 2,
      step: 3,
      payload: {
        provider: 'bole',
        model: 'qwen3.8-27b',
        reasoningEffort: 'high',
        maxTokens: 8192,
        reason: 'initial',
      },
    })
  })

  it('projects the authoritative assistant message and truncates each field', () => {
    const current = state()
    const text = 'x'.repeat(300 * 1024)
    const projected = projectSessionEvent(event('assistant/message', {
      turn: 1,
      step: 1,
      message: {
        id: 'm-1',
        content: [
          { type: 'text', text },
          { type: 'reasoning', text: 'why' },
        ],
      },
      usage: { inputTokens: 1, outputTokens: 2, totalTokens: 3 },
      interrupted: true,
    }), current, options)
    expect(projected?.type).toBe(22)
    expect(projected?.payload.text).toHaveLength(256 * 1024)
    expect(projected?.payload.reasoning).toBe('why')
    expect(projected?.payload.truncated).toBe(true)
    expect(projected?.payload.usage).toEqual({ inputTokens: 1, outputTokens: 2, totalTokens: 3 })
    expect(projected?.payload.interrupted).toBe(true)
  })

  it('keeps tool calls out of assistant text and uses placeholders only for tool results', () => {
    const current = state()
    const assistant = projectSessionEvent(event('assistant/message', {
      turn: 1,
      step: 1,
      message: {
        id: 'm-tool',
        content: [
          { type: 'text', text: 'reading' },
          { type: 'tool-call', id: 'call-1', name: 'read', arguments: '{}' },
        ],
      },
    }), current, options)
    expect(assistant?.payload.text).toBe('reading')

    const result = projectSessionEvent(event('tool/result', {
      turn: 1,
      step: 1,
      message: {
        toolCallId: 'call-1',
        content: [{ type: 'text', text: 'x' }, { type: 'image', attachment: { id: 'a' } }],
      },
    }), current, options)
    expect(result?.payload.resultSummary).toBe('x[image]')
  })

  it('projects tool calls, correlates the result name, and bounds args/summary', () => {
    const current = state()
    current.turn = 1
    current.step = 1
    expect(projectSessionEvent(event('tool/call', {
      turn: 1,
      step: 1,
      callId: 'call-1',
      name: 'read',
      arguments: '{"file_path":"README.md"}',
    }), current, options)?.payload).toMatchObject({
      callId: 'call-1',
      tool: 'read',
      args: { file_path: 'README.md' },
    })
    const result = projectSessionEvent(event('tool/result', {
      turn: 1,
      step: 1,
      message: {
        toolCallId: 'call-1',
        isError: false,
        content: [{ type: 'text', text: 'y'.repeat(10 * 1024) }],
      },
    }), current, options)
    expect(result?.type).toBe(31)
    expect(result?.payload.tool).toBe('read')
    expect(result?.payload.truncated).toBe(true)
    expect((result?.payload.resultSummary as string).length).toBe(8 * 1024)

    const raw = projectSessionEvent(event('tool/call', {
      turn: 1,
      step: 1,
      callId: 'call-2',
      name: 'bash',
      arguments: 'not json',
    }), current, options)
    expect(raw?.payload.argsRaw).toBe('not json')
  })

  it('maps completed, aborted, and failed turns', () => {
    const current = state()
    expect(projectSessionEvent(event('turn/end', { turn: 1, reason: { kind: 'completed' } }), current, options)).toMatchObject({
      type: 50,
      payload: { outcome: 'completed' },
    })
    expect(projectSessionEvent(event('turn/end', { turn: 1, reason: { kind: 'aborted', reason: { kind: 'user' } } }), current, options)).toMatchObject({
      type: 50,
      payload: { outcome: 'aborted' },
    })
    const failed = projectSessionEvent(event('turn/end', {
      turn: 1,
      reason: { kind: 'error', error: { code: 'SERVER', message: 'upstream', status: 500, requestId: 'r-1' } },
    }), current, options)
    expect(failed).toMatchObject({
      type: 51,
      payload: { code: 'SERVER', message: 'upstream', status: 500, requestId: 'r-1', retryable: true },
    })
  })

  it('projects preset selection and the experience receipt', () => {
    const current = state()
    const selected = projectSessionEvent(event('agent-preset/selected', { agentPreset: 'rsi-infer' }), current, options)
    expect(selected).toMatchObject({
      type: 15,
      payload: { scope: 'session', enabled: true, preset: 'rsi-infer', composed: true, source: 'session' },
    })
    const receipt = projectSessionEvent(event('user/message', {
      source: {
        kind: 'bole-experience',
        status: 'delivered',
        bundleId: 'qwen38-service-v3',
        deliveredMemoryIds: ['skill-1'],
        provider: 'bole',
        model: 'qwen3.8-27b',
        bodySha256: 'abc',
      },
    }), current, options)
    expect(receipt).toMatchObject({
      type: 15,
      payload: {
        source: 'receipt',
        delivery: {
          status: 'delivered',
          bundleId: 'qwen38-service-v3',
          deliveredMemoryIds: ['skill-1'],
        },
      },
    })
  })

  it('does not invent events for unknown or deferred sources', () => {
    const current = state()
    expect(projectSessionEvent(event('verify/check', {}), current, options)).toBeUndefined()
    expect(projectSessionEvent(event('session/title', {}), current, options)).toBeUndefined()
  })
})

describe('truncateUtf8', () => {
  it('never splits a multi-byte code point', () => {
    const text = '你好世界'
    const result = truncateUtf8(text, 7)
    expect(result.truncated).toBe(true)
    expect(result.text).toBe('你好')
    expect(Buffer.byteLength(result.text, 'utf8')).toBeLessThanOrEqual(7)
  })
})
