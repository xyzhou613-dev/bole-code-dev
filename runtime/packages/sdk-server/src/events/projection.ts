/**
 * Projection of harness session events into the SDK event model.
 *
 * The projector is deliberately a pure function plus a small per-session
 * state. It never invents a phase: an event without a matching harness source
 * returns `undefined`, and `VERIFY_CHECK` is never produced.
 *
 * Contract: `docs/bole-sdk/03-events.md`.
 */

/** Event type values from 03-events.md. */
export const SdkEventType = {
  TASK_STARTED: 10,
  STEP_STARTED: 11,
  STEP_FINISHED: 12,
  PLAN_UPDATED: 13,
  MODEL_SELECTED: 14,
  RSI_STATE: 15,
  ASSISTANT_MESSAGE: 22,
  TOOL_CALL_STARTED: 30,
  TOOL_CALL_FINISHED: 31,
  TASK_FINISHED: 50,
  TASK_FAILED: 51,
} as const

/** One projected SDK event, before the server assigns `event_id`. */
export interface ProjectedEvent {
  type: number
  task: number
  step: number
  seq: number
  ts_ms: number
  payload: Record<string, unknown>
}

/** Per-session projection facts shared across events. */
export interface ProjectionState {
  turn: number
  step: number
  preset: string | undefined
  /** Tool name by call id, used to enrich tool results. */
  readonly tools: Map<string, string>
}

/** Facts the projector needs from the runtime. */
export interface ProjectionOptions {
  sessionId: string
  composed: boolean
}

/** Minimal shape of a session event; plugin-augmented events are read structurally. */
export interface RawSessionEvent {
  type: string
  seq: number
  time: number
  data: Record<string, unknown>
}

const TRUNCATE_ARGS = 64 * 1024
const TRUNCATE_RESULT = 8 * 1024
const TRUNCATE_MESSAGE = 256 * 1024

/**
 * Cut a string at a UTF-8 byte bound without splitting a code point.
 * @param text - source text.
 * @param maxBytes - maximum UTF-8 bytes.
 * @returns the bounded text and whether it was cut.
 */
export function truncateUtf8(text: string, maxBytes: number): { text: string; truncated: boolean } {
  if (Buffer.byteLength(text, 'utf8') <= maxBytes) return { text, truncated: false }
  let bytes = 0
  let result = ''
  for (const character of text) {
    const size = Buffer.byteLength(character, 'utf8')
    if (bytes + size > maxBytes) break
    result += character
    bytes += size
  }
  return { text: result, truncated: true }
}

/**
 * Join text blocks and mark non-text blocks with a placeholder.
 * @param content - message content blocks.
 * @returns joined text.
 */
function joinText(content: readonly { type?: unknown; text?: unknown }[]): string {
  let result = ''
  for (const block of content) {
    if (block.type === 'text' && typeof block.text === 'string') result += block.text
    else if (block.type === 'image') result += '[image]'
    else if (block.type !== undefined) result += `[${String(block.type)}]`
  }
  return result
}

/**
 * Join assistant reasoning blocks.
 * @param content - message content blocks.
 * @returns joined reasoning text.
 */
function joinReasoning(content: readonly { type?: unknown; text?: unknown }[]): string {
  let result = ''
  for (const block of content) {
    if (block.type === 'reasoning' && typeof block.text === 'string') result += block.text
  }
  return result
}

/**
 * Map a turn end reason to the SDK outcome.
 * @param kind - `turn/end.reason.kind`.
 * @returns the outcome string, or undefined for an unknown kind.
 */
function outcomeOf(kind: unknown): string | undefined {
  switch (kind) {
    case 'completed': return 'completed'
    case 'aborted': return 'aborted'
    case 'max-tokens': return 'max_tokens'
    case 'blocked': return 'blocked'
    case 'interrupted': return 'interrupted'
    case 'forked': return 'forked'
    default: return undefined
  }
}

/**
 * Decide whether a model failure is worth retrying.
 * @param code - failure code.
 * @param message - failure message.
 * @param status - optional HTTP status.
 * @returns whether the caller may retry.
 */
function retryable(code: string, message: string, status: number | undefined): boolean {
  if (status === 429 || (status !== undefined && status >= 500)) return true
  return /RATE_LIMIT|TIMEOUT|NETWORK|ECONN|FETCH|503|502|500/i.test(`${code} ${message}`)
}

/**
 * Build the RSI state payload from the projection state.
 * @param state - session projection state.
 * @param options - composition facts.
 * @param source - why the state changed.
 * @param scope - default or session scope.
 * @param delivery - optional receipt payload.
 * @returns the RSI state payload.
 */
export function rsiPayload(
  state: ProjectionState,
  options: Pick<ProjectionOptions, 'composed' | 'sessionId'>,
  source: 'config' | 'interface' | 'session' | 'receipt',
  scope: 'default' | 'session',
  delivery?: Record<string, unknown>,
): Record<string, unknown> {
  const preset = options.composed ? state.preset ?? '' : ''
  return {
    v: 1,
    scope,
    ...(scope === 'session' ? { sessionId: options.sessionId } : {}),
    enabled: preset === 'rsi-infer',
    preset,
    composed: options.composed,
    source,
    ...(delivery === undefined ? {} : { delivery }),
  }
}

/**
 * Project one session event.
 * @param event - raw session event.
 * @param state - mutable per-session projection state.
 * @param options - session identity and preset-composition facts.
 * @returns the SDK event, or undefined when the source has no mapping.
 */
export function projectSessionEvent(
  event: RawSessionEvent,
  state: ProjectionState,
  options: ProjectionOptions,
): ProjectedEvent | undefined {
  const base = (type: number, task: number, step: number, payload: Record<string, unknown>): ProjectedEvent => ({
    type,
    task,
    step,
    seq: event.seq,
    ts_ms: event.time,
    payload: { v: 1, ...payload },
  })
  switch (event.type) {
    case 'turn/start': {
      state.turn = Number(event.data.turn ?? 0)
      state.step = 0
      return base(SdkEventType.TASK_STARTED, state.turn, 0, { turn: state.turn })
    }
    case 'step/start': {
      state.turn = Number(event.data.turn ?? state.turn)
      state.step = Number(event.data.step ?? 0)
      return base(SdkEventType.STEP_STARTED, state.turn, state.step, {
        turn: state.turn,
        step: state.step,
      })
    }
    case 'step/end': {
      const turn = Number(event.data.turn ?? state.turn)
      const step = Number(event.data.step ?? state.step)
      return base(SdkEventType.STEP_FINISHED, turn, step, { turn, step })
    }
    case 'plan/mode':
      return base(SdkEventType.PLAN_UPDATED, state.turn, state.step, {
        kind: 'plan_mode',
        active: event.data.active === true,
      })
    case 'todo/write':
      return base(SdkEventType.PLAN_UPDATED, state.turn, state.step, {
        kind: 'todos',
        todos: Array.isArray(event.data.todos) ? event.data.todos : [],
      })
    case 'request/header': {
      const header = event.data.header as Record<string, unknown> | undefined
      const config = (header?.config ?? {}) as Record<string, unknown>
      const payload: Record<string, unknown> = {
        provider: config.provider,
        model: config.model,
        reason: event.data.reason,
      }
      if (config.reasoningEffort !== undefined) payload.reasoningEffort = config.reasoningEffort
      if (config.maxTokens !== undefined) payload.maxTokens = config.maxTokens
      return base(SdkEventType.MODEL_SELECTED, state.turn, state.step, payload)
    }
    case 'assistant/message': {
      const data = event.data
      const turn = Number(data.turn ?? state.turn)
      const step = Number(data.step ?? state.step)
      const message = (data.message ?? {}) as Record<string, unknown>
      const content = Array.isArray(message.content) ? message.content as { type?: unknown; text?: unknown }[] : []
      const text = truncateUtf8(joinText(content), TRUNCATE_MESSAGE)
      const reasoning = truncateUtf8(joinReasoning(content), TRUNCATE_MESSAGE)
      const payload: Record<string, unknown> = {
        turn,
        step,
        messageId: message.id,
        text: text.text,
        reasoning: reasoning.text,
      }
      if (data.usage !== undefined) payload.usage = data.usage
      if (data.interrupted === true) payload.interrupted = true
      if (text.truncated || reasoning.truncated) payload.truncated = true
      return base(SdkEventType.ASSISTANT_MESSAGE, turn, step, payload)
    }
    case 'tool/call': {
      const turn = Number(event.data.turn ?? state.turn)
      const step = Number(event.data.step ?? state.step)
      const callId = String(event.data.callId ?? '')
      const tool = String(event.data.name ?? '')
      state.tools.set(callId, tool)
      const raw = typeof event.data.arguments === 'string' ? event.data.arguments : ''
      let args: unknown
      let argsRaw = false
      try {
        args = JSON.parse(raw)
      } catch {
        args = raw
        argsRaw = true
      }
      const payload: Record<string, unknown> = { callId, tool }
      if (argsRaw) {
        const bounded = truncateUtf8(String(args), TRUNCATE_ARGS)
        payload.argsRaw = bounded.text
        if (bounded.truncated) payload.truncated = true
      } else {
        const serialized = JSON.stringify(args)
        if (Buffer.byteLength(serialized, 'utf8') > TRUNCATE_ARGS) {
          payload.truncated = true
        } else {
          payload.args = args
        }
      }
      return base(SdkEventType.TOOL_CALL_STARTED, turn, step, payload)
    }
    case 'tool/result': {
      const data = event.data
      const turn = Number(data.turn ?? state.turn)
      const step = Number(data.step ?? state.step)
      const message = (data.message ?? {}) as Record<string, unknown>
      const callId = String(message.toolCallId ?? '')
      const content = Array.isArray(message.content) ? message.content as { type?: unknown; text?: unknown }[] : []
      const summary = truncateUtf8(joinText(content), TRUNCATE_RESULT)
      const payload: Record<string, unknown> = {
        callId,
        isError: message.isError === true,
        resultSummary: summary.text,
        truncated: summary.truncated,
      }
      const tool = state.tools.get(callId)
      if (tool !== undefined) payload.tool = tool
      if (data.error !== undefined) payload.error = data.error
      return base(SdkEventType.TOOL_CALL_FINISHED, turn, step, payload)
    }
    case 'turn/end': {
      const data = event.data
      const turn = Number(data.turn ?? state.turn)
      const reason = (data.reason ?? {}) as Record<string, unknown>
      const kind = reason.kind
      if (kind === 'error') {
        const failure = (reason.error ?? {}) as Record<string, unknown>
        const code = typeof failure.code === 'string' ? failure.code : 'UNKNOWN'
        const message = typeof failure.message === 'string' ? failure.message : ''
        const status = typeof failure.status === 'number' ? failure.status : undefined
        const payload: Record<string, unknown> = {
          turn,
          code,
          message,
          retryable: retryable(code, message, status),
        }
        if (status !== undefined) payload.status = status
        if (failure.providerRetryAfterMs !== undefined) payload.providerRetryAfterMs = failure.providerRetryAfterMs
        if (failure.requestId !== undefined) payload.requestId = failure.requestId
        return base(SdkEventType.TASK_FAILED, turn, 0, payload)
      }
      const outcome = outcomeOf(kind)
      if (outcome === undefined) return undefined
      return base(SdkEventType.TASK_FINISHED, turn, 0, {
        turn,
        outcome,
        reason,
      })
    }
    case 'agent-preset/selected': {
      const preset = typeof event.data.agentPreset === 'string' ? event.data.agentPreset : ''
      state.preset = preset
      return base(SdkEventType.RSI_STATE, 0, 0, rsiPayload(state, options, 'session', 'session'))
    }
    case 'user/message': {
      const source = (event.data.source ?? {}) as Record<string, unknown>
      if (source.kind !== 'bole-experience') return undefined
      const delivery: Record<string, unknown> = {
        status: source.status,
        bundleId: source.bundleId,
        deliveredMemoryIds: source.deliveredMemoryIds,
        provider: source.provider,
        model: source.model,
        bodySha256: source.bodySha256,
      }
      return base(SdkEventType.RSI_STATE, 0, 0, rsiPayload(state, options, 'receipt', 'session', delivery))
    }
    default:
      return undefined
  }
}
