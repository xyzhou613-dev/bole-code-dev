/**
 * Session-aware JSON-RPC server for the Bole SDK runtime.
 *
 * Forked from `@deepseek-ai/dsh-sdk-jsonrpc-server` (0.1.7-alpha.2) and
 * extended with the Bole session methods. The fork is intentional: the
 * official server keeps its session table private, creates agents without a
 * preset composition, and exposes only three methods. Keeping the official
 * initialize/prompt/shutdown semantics here avoids a second source of truth
 * for attachment admission and shutdown ordering.
 *
 * Contract: `docs/bole-sdk/02-protocol.md` and `03-events.md`.
 */

import type { Context } from '@deepseek-ai/cordis'
import { isAbsolute, resolve } from 'node:path'
import type { Agent, AgentHandle, AgentOptions, AgentSetup } from '@deepseek-ai/dsh-agent'
import type {} from '@deepseek-ai/dsh-agent-preset-registry'
import { admitEncodedImages, type EncodedImageAttachment, type ImageAttachmentRef } from '@deepseek-ai/dsh-attachment'
import { createUserMessage, ReasoningEffortId, type ContentBlock, type LlmRuntime } from '@deepseek-ai/dsh-llm'
import * as LlmDeepSeek from '@deepseek-ai/dsh-llm-deepseek'
import type { SessionEvent, SessionId } from '@deepseek-ai/dsh-session'
import {
  SessionAlreadyExistsError,
  SessionAlreadyOwnedError,
  SessionPersistenceNotFoundError,
} from '@deepseek-ai/dsh-session-persistence'
import { SessionQueryError } from '@deepseek-ai/dsh-session-query'
import type { JsonRpcTransportPeer, InitializeParams, InitializeResult, SessionEventNotification, SessionPromptParams, SessionPromptResult, SdkEncodedImageBlock } from '@deepseek-ai/dsh-sdk-protocol'
import { BoleRpcError } from './transport.js'
import { DeltaCoalescer } from './events/deltas.js'
import { projectSessionEvent, rsiPayload, type ProjectionState, type ProjectedEvent, type RawSessionEvent } from './events/projection.js'

interface SessionRecord {
  handle: AgentHandle
  preset: string | undefined
  cwd: string
}

interface SessionResult {
  sessionId: string
  preset: string
  cwd: string
}

/** Bole extension to the official handshake: select the default preset. */
interface BoleInitializeParams extends InitializeParams {
  rsiEnabled?: boolean
}

function invalid(message: string): BoleRpcError {
  return new BoleRpcError(-32602, message, { code: 'INVALID_ARGUMENT' })
}

function business(code: string, message: string): BoleRpcError {
  return new BoleRpcError(-32603, message, { code })
}

function requireString(value: unknown, field: string): string {
  if (typeof value !== 'string' || value.trim() === '') throw invalid(`${field} must be a non-empty string`)
  return value
}

function optionalString(value: unknown, field: string): string | undefined {
  if (value === undefined) return undefined
  return requireString(value, field)
}

function requireAbsolute(value: unknown, field: string): string {
  const path = requireString(value, field)
  if (!isAbsolute(path)) throw invalid(`${field} must be an absolute path`)
  return resolve(path)
}

function encodedImage(block: SessionPromptParams['contentBlocks'][number]): block is SdkEncodedImageBlock {
  return block.type === 'image' && 'data' in block
}

/**
 * Validate content blocks before attachment admission.
 * @param blocks - raw protocol content blocks.
 * @returns the same blocks once their shape is valid.
 */
function validateContentBlocks(blocks: unknown): SessionPromptParams['contentBlocks'] {
  if (!Array.isArray(blocks) || blocks.length === 0) throw invalid('contentBlocks must be a non-empty array')
  for (const block of blocks) {
    if (block === null || typeof block !== 'object') throw invalid('content block must be an object')
    const type = (block as { type?: unknown }).type
    if (type === 'text') {
      const text = (block as { text?: unknown }).text
      if (typeof text !== 'string' || text.length === 0) throw invalid('text content block requires non-empty text')
      continue
    }
    if (type === 'image') {
      const data = (block as { data?: unknown }).data
      const mimeType = (block as { mimeType?: unknown }).mimeType
      if (typeof data !== 'string' || data.length === 0) throw invalid('image content block requires base64 data')
      if (mimeType !== 'image/png' && mimeType !== 'image/jpeg' && mimeType !== 'image/webp' && mimeType !== 'image/gif') {
        throw invalid('image content block has an unsupported mimeType')
      }
      continue
    }
    throw invalid(`unsupported content block type: ${String(type)}`)
  }
  return blocks as SessionPromptParams['contentBlocks']
}

/**
 * Admit inline images and return durable prompt content.
 * @param ctx - profile context carrying the attachment store.
 * @param blocks - validated protocol blocks.
 * @returns content blocks whose images reference the attachment store.
 */
async function durablePromptContent(ctx: Context, blocks: SessionPromptParams['contentBlocks']): Promise<ContentBlock[]> {
  const images = blocks.filter(encodedImage)
  if (images.length === 0) return blocks as ContentBlock[]
  const attachments = ctx.get('attachments')
  if (attachments === undefined) throw invalid('image prompt requires an attachment store')
  let refs: readonly ImageAttachmentRef[]
  try {
    refs = await admitEncodedImages(attachments, images.map((image): EncodedImageAttachment => ({
      data: image.data,
      mediaType: image.mimeType,
    })))
  } catch (error) {
    throw invalid(`invalid image content: ${error instanceof Error ? error.message : String(error)}`)
  }
  let next = 0
  return blocks.map(block => encodedImage(block)
    ? { type: 'image', attachment: refs[next++] as ImageAttachmentRef }
    : block)
}

/** Read the last preset selection from a durable log; the header is the creation value. */
function presetFromLog(events: readonly SessionEvent[]): string | undefined {
  let preset: string | undefined
  for (const event of events) {
    if (event.type === 'agent-preset/selected') preset = event.data.agentPreset
  }
  return preset
}

/**
 * Map a preset registry failure to the stable SDK error name.
 * @param error - failure from resolve/mount/select.
 * @returns the protocol error to surface.
 */
function presetError(error: unknown): BoleRpcError {
  const record = error as { code?: unknown; message?: unknown } | null
  const code = record?.code === undefined ? '' : String(record.code)
  const message = typeof record?.message === 'string' ? record.message : String(error)
  if (code === 'agent-preset/locked') return business('PRESET_LOCKED', message)
  if (code === 'agent-preset/not-found' || code === 'agent-preset/invalid') {
    return business('PRESET_NOT_FOUND', message)
  }
  return business('INTERNAL', message)
}

/** Whether a failure came from the preset registry rather than the agent factory. */
function isPresetFailure(error: unknown): boolean {
  const code = (error as { code?: unknown } | null)?.code
  return typeof code === 'string' && code.startsWith('agent-preset/')
}

/**
 * Session-aware SDK server over one booted context and transport peer.
 * Reinitialization is unsupported; `shutdown` is idempotent.
 */
export class BoleSdkServer {
  private cwd = process.cwd()
  private provider = 'deepseek-official'
  private model = 'deepseek-official'
  private reasoningEffort: ReturnType<typeof ReasoningEffortId> | undefined
  private maxTokens: number | undefined
  private defaultPreset: string | undefined
  private llmFiber: { dispose(): Promise<void> } | undefined
  private readonly sessions = new Map<string, SessionRecord>()
  private readonly sessionCreations = new Map<string, Promise<SessionRecord>>()
  private readonly projections = new Map<string, ProjectionState>()
  private readonly deltas = new DeltaCoalescer((delta) => { this.transport.notify('assistant.delta', delta) })
  private readonly disposers: (() => void)[] = []
  private nextEventId = 1
  private shutdownTask: Promise<Record<string, never>> | undefined
  private shuttingDown = false
  private initialized = false

  /**
   * @param ctx - profile context.
   * @param transport - outbound notification peer.
   */
  constructor(
    private readonly ctx: Context,
    private readonly transport: JsonRpcTransportPeer,
  ) {
    this.disposers.push(ctx.on('session/event', (session, event) => {
      const sessionId = String(session.id)
      const payload: SessionEventNotification = { sessionId, event }
      this.transport.notify('session.event', payload)
      if (event.type === 'assistant/message' || event.type === 'turn/end') {
        this.deltas.flushSession(sessionId)
      }
      if (this.sessions.has(sessionId)) this.project(sessionId, event)
    }))
    this.disposers.push(ctx.on('agent/assistant-stream', ({ agent, frame }) => {
      const sessionId = String(agent.session.id)
      if (!this.sessions.has(sessionId)) return
      this.deltas.push(sessionId, frame)
    }))
    this.disposers.push(ctx.on('agent/status', ({ agent, status }) => {
      this.transport.notify('session.status', { sessionId: String(agent.session.id), status })
    }))
  }

  /**
   * Validate the model route and establish the default cwd/preset.
   * @param params - SDK handshake parameters.
   * @returns server identity; capabilities are added by the plugin entry.
   */
  async initialize(params: BoleInitializeParams): Promise<InitializeResult> {
    if (this.shuttingDown) throw business('INTERNAL', 'SDK server is shutting down')
    const cwd = requireAbsolute(params.cwd, 'cwd')
    const provider = requireString(params.provider, 'provider')
    const model = requireString(params.model, 'model')
    if (params.reasoningEffort !== undefined && (typeof params.reasoningEffort !== 'string' || params.reasoningEffort.length === 0)) {
      throw invalid('reasoningEffort must be a non-empty string')
    }
    if (params.maxTokens !== undefined && (!Number.isSafeInteger(params.maxTokens) || params.maxTokens <= 0)) {
      throw invalid('maxTokens must be a positive safe integer')
    }
    if (params.rsiEnabled !== undefined && typeof params.rsiEnabled !== 'boolean') {
      throw invalid('rsiEnabled must be a boolean')
    }
    const reasoningEffort = params.reasoningEffort === undefined
      ? undefined
      : ReasoningEffortId(params.reasoningEffort)
    if (!this.hasAdapterFor(provider)) {
      if (provider !== 'deepseek-official') throw business('MODEL_NOT_FOUND', `no adapter registered for provider "${provider}"`)
      this.llmFiber = await this.ctx.plugin(LlmDeepSeek)
    }
    const llm = this.ctx.get('llm') as LlmRuntime | undefined
    if (llm === undefined) throw business('MODEL_NOT_FOUND', 'no LLM runtime is mounted')
    try {
      await llm.resolveCallConfig({
        provider,
        model,
        ...reasoningEffort === undefined ? {} : { reasoningEffort },
        ...params.maxTokens === undefined ? {} : { maxTokens: params.maxTokens },
      })
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error)
      throw business(/auth|api key|credential/i.test(message) ? 'MODEL_AUTH_FAILED' : 'MODEL_NOT_FOUND', message)
    }
    this.cwd = cwd
    this.provider = provider
    this.model = model
    this.reasoningEffort = reasoningEffort
    this.maxTokens = params.maxTokens
    this.initialized = true
    if (typeof params.rsiEnabled === 'boolean' && this.ctx.get('agentPresets') !== undefined) {
      this.defaultPreset = params.rsiEnabled ? 'rsi-infer' : 'standard'
    }
    setImmediate(() => { this.emitDefaultRsi('config') })
    return { serverInfo: { name: 'deepseek-harness-sdk-runtime', version: '0.0.1' } }
  }

  /**
   * Create a session without touching the model or tools.
   * @param params - session id, optional cwd, optional preset.
   * @returns the created session identity.
   */
  async create(params: Record<string, unknown>): Promise<SessionResult> {
    this.assertInitialized()
    const sessionId = requireString(params.sessionId, 'sessionId')
    const cwd = params.cwd === undefined ? this.cwd : requireAbsolute(params.cwd, 'cwd')
    const preset = optionalString(params.preset, 'preset')
    if (this.sessions.has(sessionId) || this.sessionCreations.has(sessionId)) {
      throw business('SESSION_EXISTS', `session "${sessionId}" already exists`)
    }
    return this.toResult(sessionId, await this.createRecord(sessionId, cwd, preset))
  }

  /**
   * Resume a persisted session and compose its recorded preset.
   * @param params - session id.
   * @returns the resumed session identity.
   */
  async resume(params: Record<string, unknown>): Promise<SessionResult> {
    this.assertInitialized()
    const sessionId = requireString(params.sessionId, 'sessionId')
    const existing = this.sessions.get(sessionId)
    if (existing !== undefined) return this.toResult(sessionId, existing)
    const query = this.ctx.get('sessionQuery')
    const persistence = this.ctx.get('sessionPersistence')
    if (query === undefined || persistence === undefined) {
      throw business('PERSISTENCE_UNAVAILABLE', 'session persistence and query services are required to resume')
    }
    let snapshot
    try {
      snapshot = await query.readSession(sessionId as SessionId)
    } catch (error) {
      if (error instanceof SessionQueryError && error.code === 'SESSION_QUERY_SESSION_NOT_FOUND') {
        throw business('SESSION_NOT_FOUND', `session "${sessionId}" not found`)
      }
      if (error instanceof SessionPersistenceNotFoundError) {
        throw business('SESSION_NOT_FOUND', `session "${sessionId}" not found`)
      }
      throw business('SESSION_NOT_RESUMABLE', `session "${sessionId}" cannot be read: ${error instanceof Error ? error.message : String(error)}`)
    }
    const preset = presetFromLog(snapshot.events) ?? snapshot.session.agentPreset
    const composition = await this.compose(preset)
    let handle: AgentHandle
    try {
      handle = await this.ctx.agents.resume({
        resumeSessionId: sessionId as SessionId,
        agentOptions: this.agentOptions(),
        ...(composition.setup === undefined ? {} : { setup: composition.setup }),
      })
    } catch (error) {
      if (error instanceof SessionPersistenceNotFoundError) {
        throw business('SESSION_NOT_FOUND', `session "${sessionId}" not found`)
      }
      if (error instanceof SessionAlreadyOwnedError || error instanceof SessionAlreadyExistsError) {
        throw business('SESSION_NOT_RESUMABLE', `session "${sessionId}" cannot be resumed: ${error.message}`)
      }
      if (isPresetFailure(error)) throw presetError(error)
      throw business('SESSION_NOT_RESUMABLE', `session "${sessionId}" cannot be resumed: ${error instanceof Error ? error.message : String(error)}`)
    }
    const record: SessionRecord = {
      handle,
      preset: composition.agentPreset ?? preset,
      cwd: snapshot.session.cwd ?? this.cwd,
    }
    this.sessions.set(sessionId, record)
    this.projections.set(sessionId, {
      turn: 0,
      step: 0,
      preset: record.preset,
      tools: new Map(),
    })
    setImmediate(() => { this.emitRsi(sessionId, record) })
    return this.toResult(sessionId, record)
  }

  /**
   * Queue one prompt, lazily creating an unknown session.
   * @param params - target session and content.
   * @returns the durable message identity.
   */
  async prompt(params: SessionPromptParams): Promise<SessionPromptResult> {
    this.assertInitialized()
    const sessionId = requireString(params.sessionId, 'sessionId')
    const blocks = validateContentBlocks(params.contentBlocks)
    const record = await this.getOrCreateSession(sessionId)
    this.assertLive(sessionId, record)
    const content = await durablePromptContent(this.ctx, blocks)
    const message = createUserMessage({ content, source: { kind: 'user' } })
    record.handle.agent.followup(message)
    return { messageId: String(message.id) }
  }

  /**
   * Interrupt the active work for one session.
   * @param params - target session.
   * @returns an empty result once accepted.
   */
  async cancel(params: Record<string, unknown>): Promise<Record<string, never>> {
    this.assertInitialized()
    const sessionId = requireString(params.sessionId, 'sessionId')
    const record = this.sessions.get(sessionId)
    const agent = record?.handle.agent ?? this.ctx.agents.get(sessionId as SessionId)
    if (agent === undefined) throw business('SESSION_NOT_FOUND', `session "${sessionId}" not found`)
    const boundary = this.ctx.get('sessionProjections')?.stateOf(agent.session, 'turnBoundary')
    const active = (boundary !== undefined && boundary.openTurnStartSeq !== null)
      || agent.inbox.nextTurn.length > 0
      || agent.inbox.nextStep.length > 0
      || agent.status === 'running'
    if (!active) throw business('NO_ACTIVE_TASK', `session "${sessionId}" has no active task`)
    agent.cancel({ kind: 'user' }, { keepInbox: false })
    return {}
  }

  /**
   * Select a preset for a blank session.
   * @param params - target session and preset id.
   * @returns the committed preset.
   */
  async setPreset(params: Record<string, unknown>): Promise<{ sessionId: string; preset: string }> {
    this.assertInitialized()
    const sessionId = requireString(params.sessionId, 'sessionId')
    const preset = requireString(params.preset, 'preset')
    const record = this.sessions.get(sessionId)
    if (record === undefined) throw business('SESSION_NOT_FOUND', `session "${sessionId}" not found`)
    const presets = this.ctx.get('agentPresets')
    if (presets === undefined) throw business('RSI_UNAVAILABLE', 'the runtime has no preset plane')
    this.assertLive(sessionId, record)
    try {
      record.preset = await presets.select(record.handle.agent, preset)
      return { sessionId, preset: record.preset }
    } catch (error) {
      throw presetError(error)
    }
  }

  /**
   * Set the default preset for new sessions, or select it for one blank session.
   * @param params - enabled flag and optional session id.
   * @returns the committed scope.
   */
  async rsiSet(params: Record<string, unknown>): Promise<{ enabled: boolean; preset: string; scope: string }> {
    this.assertInitialized()
    if (typeof params.enabled !== 'boolean') throw invalid('enabled must be a boolean')
    const presets = this.ctx.get('agentPresets')
    if (presets === undefined) throw business('RSI_UNAVAILABLE', 'the runtime has no preset plane')
    const preset = params.enabled ? 'rsi-infer' : 'standard'
    const sessionId = optionalString(params.sessionId, 'sessionId')
    if (sessionId !== undefined) {
      await this.setPreset({ sessionId, preset })
      return { enabled: params.enabled, preset, scope: 'session' }
    }
    try {
      await presets.resolve(preset)
    } catch (error) {
      throw presetError(error)
    }
    this.defaultPreset = preset
    this.emitDefaultRsi('interface')
    return { enabled: params.enabled, preset, scope: 'default' }
  }

  /**
   * Read the default or one session's preset state.
   * @param params - optional session id.
   * @returns enabled/preset/composed.
   */
  async rsiGet(params: Record<string, unknown>): Promise<{ enabled: boolean; preset: string; composed: boolean }> {
    this.assertInitialized()
    const presets = this.ctx.get('agentPresets')
    const composed = presets !== undefined
    if (!composed) return { enabled: false, preset: '', composed: false }
    const sessionId = optionalString(params.sessionId, 'sessionId')
    if (sessionId !== undefined) {
      const record = this.sessions.get(sessionId)
      if (record === undefined) throw business('SESSION_NOT_FOUND', `session "${sessionId}" not found`)
      const preset = record.preset ?? presets.defaultId
      return { enabled: preset === 'rsi-infer', preset, composed: true }
    }
    const preset = this.defaultPreset ?? presets.defaultId
    return { enabled: preset === 'rsi-infer', preset, composed: true }
  }

  /**
   * Dispose server-owned sessions, subscriptions, and the fallback adapter.
   * @returns an empty result.
   */
  shutdown(): Promise<Record<string, never>> {
    this.shutdownTask ??= this.performShutdown()
    return this.shutdownTask
  }

  /**
   * Dispatch one JSON-RPC request.
   * @param method - method name.
   * @param params - raw params object.
   * @returns the method result.
   */
  async handleRequest(method: string, params: Record<string, unknown> | undefined): Promise<unknown> {
    switch (method) {
      case 'initialize':
        return this.initialize(params as unknown as BoleInitializeParams)
      case 'session/create':
        return this.create(params ?? {})
      case 'session/resume':
        return this.resume(params ?? {})
      case 'session/prompt':
        return this.prompt(params as unknown as SessionPromptParams)
      case 'session/cancel':
        return this.cancel(params ?? {})
      case 'session/setPreset':
        return this.setPreset(params ?? {})
      case 'rsi.set':
        return this.rsiSet(params ?? {})
      case 'rsi.get':
        return this.rsiGet(params ?? {})
      case 'shutdown':
        return this.shutdown()
      default:
        throw new BoleRpcError(-32601, `method not found: ${method}`, { code: 'METHOD_NOT_FOUND' })
    }
  }

  private async createRecord(sessionId: string, cwd: string, presetId: string | undefined): Promise<SessionRecord> {
    if (this.ctx.agents.get(sessionId as SessionId) !== undefined) {
      throw business('SESSION_EXISTS', `session "${sessionId}" already exists`)
    }
    const persistence = this.ctx.get('sessionPersistence')
    if (persistence !== undefined && await persistence.stat(sessionId as SessionId) !== undefined) {
      throw business('SESSION_EXISTS', `session "${sessionId}" already exists`)
    }
    const composition = await this.compose(presetId)
    let handle: AgentHandle
    try {
      handle = await this.ctx.agents.create({
        sessionId: sessionId as SessionId,
        meta: {
          cwd,
          ...(composition.agentPreset === undefined ? {} : { agentPreset: composition.agentPreset }),
        },
        agentOptions: this.agentOptions(),
        ...(composition.setup === undefined ? {} : { setup: composition.setup }),
      })
    } catch (error) {
      if (error instanceof SessionAlreadyExistsError) {
        throw business('SESSION_EXISTS', `session "${sessionId}" already exists`)
      }
      if (isPresetFailure(error)) throw presetError(error)
      throw business('INTERNAL', `session "${sessionId}" could not be created: ${error instanceof Error ? error.message : String(error)}`)
    }
    const record: SessionRecord = { handle, preset: composition.agentPreset, cwd }
    this.sessions.set(sessionId, record)
    this.projections.set(sessionId, {
      turn: 0,
      step: 0,
      preset: composition.agentPreset,
      tools: new Map(),
    })
    setImmediate(() => { this.emitRsi(sessionId, record) })
    return record
  }

  private async getOrCreateSession(sessionId: string): Promise<SessionRecord> {
    const existing = this.sessions.get(sessionId)
    if (existing !== undefined) return existing
    const pending = this.sessionCreations.get(sessionId)
    if (pending !== undefined) return pending
    const creation = this.createRecord(sessionId, this.cwd, this.newSessionPreset()).finally(() => {
      if (this.sessionCreations.get(sessionId) === creation) this.sessionCreations.delete(sessionId)
    })
    this.sessionCreations.set(sessionId, creation)
    return creation
  }

  /**
   * Compose a preset before publication. A preset id without a preset plane is
   * rejected rather than silently dropped.
   * @param presetId - requested preset or the registry default.
   * @returns the durable preset id and pre-publication setup.
   */
  private async compose(presetId: string | undefined): Promise<{ agentPreset?: string; setup?: AgentSetup }> {
    const presets = this.ctx.get('agentPresets')
    if (presets === undefined) {
      if (presetId !== undefined) throw business('PRESET_NOT_FOUND', `agent preset "${presetId}" is unavailable`)
      return {}
    }
    let resolved
    try {
      resolved = await presets.resolve(presetId)
    } catch (error) {
      throw presetError(error)
    }
    if (resolved.broken !== undefined) throw business('PRESET_NOT_FOUND', resolved.broken)
    return {
      agentPreset: resolved.id,
      setup: async (agentCtx) => {
        try {
          await presets.mount(agentCtx, resolved.id)
        } catch (error) {
          throw presetError(error)
        }
      },
    }
  }

  private newSessionPreset(): string | undefined {
    const presets = this.ctx.get('agentPresets')
    if (presets === undefined) return undefined
    return this.defaultPreset ?? presets.defaultId
  }

  /**
   * Project one session event and publish it as `bole.event`.
   * @param sessionId - owning session.
   * @param event - raw session event.
   */
  private project(sessionId: string, event: { type: string; seq: number; time: number; data: unknown }): void {
    const state = this.projections.get(sessionId)
    if (state === undefined) return
    const projected = projectSessionEvent(
      event as unknown as RawSessionEvent,
      state,
      { sessionId, composed: this.ctx.get('agentPresets') !== undefined },
    )
    if (projected !== undefined) this.emit(sessionId, projected)
  }

  private emit(sessionId: string, projected: ProjectedEvent): void {
    this.transport.notify('bole.event', {
      sessionId,
      event: { event_id: this.nextEventId++, session_id: sessionId, ...projected },
    })
  }

  private emitDefaultRsi(source: 'config' | 'interface'): void {
    const composed = this.ctx.get('agentPresets') !== undefined
    const state: ProjectionState = {
      turn: 0,
      step: 0,
      preset: composed ? this.defaultPreset ?? this.ctx.get('agentPresets')?.defaultId : undefined,
      tools: new Map(),
    }
    this.emit('', {
      type: 15,
      task: 0,
      step: 0,
      seq: 0,
      ts_ms: Date.now(),
      payload: rsiPayload(state, { sessionId: '', composed }, source, 'default'),
    })
  }

  private emitRsi(sessionId: string, record: SessionRecord): void {
    const state = this.projections.get(sessionId)
    if (state === undefined) return
    const composed = this.ctx.get('agentPresets') !== undefined
    const payload = rsiPayload(state, { sessionId, composed }, 'session', 'session')
    this.emit(sessionId, {
      type: 15,
      task: 0,
      step: 0,
      seq: 0,
      ts_ms: Date.now(),
      payload,
    })
    void record
  }

  private toResult(sessionId: string, record: SessionRecord): SessionResult {
    return { sessionId, preset: record.preset ?? '', cwd: record.cwd }
  }

  private agentOptions(): AgentOptions {
    return {
      provider: this.provider,
      model: this.model,
      ...(this.reasoningEffort === undefined ? {} : { reasoningEffort: this.reasoningEffort }),
      ...(this.maxTokens === undefined ? {} : { maxTokens: this.maxTokens }),
    }
  }

  private assertInitialized(): void {
    if (!this.initialized || this.shuttingDown) throw business('INTERNAL', 'SDK server is not initialized')
  }

  private assertLive(sessionId: string, record: SessionRecord): void {
    if (this.ctx.agents.get(sessionId as SessionId) !== record.handle.agent) {
      throw business('SESSION_NOT_FOUND', `session "${sessionId}" is no longer live`)
    }
  }

  private hasAdapterFor(provider: string): boolean {
    return this.ctx.get('llm')?.listProviders().some(entry => entry.id === provider) ?? false
  }

  private async performShutdown(): Promise<Record<string, never>> {
    this.shuttingDown = true
    this.deltas.flushAll()
    this.deltas.dispose()
    this.projections.clear()
    await Promise.allSettled([...this.sessionCreations.values()])
    this.sessionCreations.clear()
    const records = [...this.sessions.values()]
    this.sessions.clear()
    const failures: unknown[] = []
    while (this.disposers.length > 0) {
      try {
        this.disposers.pop()?.()
      } catch (error) {
        failures.push(error)
      }
    }
    const teardown = await Promise.allSettled([
      ...records.map(record => Promise.resolve().then(() => record.handle.dispose())),
      ...(this.llmFiber === undefined ? [] : [Promise.resolve().then(() => this.llmFiber?.dispose())]),
    ])
    this.llmFiber = undefined
    failures.push(...teardown
      .filter((result): result is PromiseRejectedResult => result.status === 'rejected')
      .map(result => result.reason as unknown))
    if (failures.length === 1) throw failures[0]
    if (failures.length > 1) throw new AggregateError(failures, 'SDK server teardown failed')
    return {}
  }
}
