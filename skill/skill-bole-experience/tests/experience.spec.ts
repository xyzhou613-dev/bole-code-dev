/** Exact-model final experiences, no-experience control, and route/disposal safety. */
import { afterEach, describe, expect, it } from 'vitest'
import type { Context } from '@deepseek-ai/cordis'
import { installModelSelection } from '@deepseek-ai/dsh-agent'
import type { ModelSelectionRef } from '@deepseek-ai/dsh-agent'
import { createUserMessage, ToolCallId } from '@deepseek-ai/dsh-llm'
import { Session, SessionLogOffset, type SessionEvent } from '@deepseek-ai/dsh-session'
import { escapeText, isModelInvocable } from '@deepseek-ai/dsh-skill'
import * as plugin from '../src/index.ts'
import { loadBundles, matchesServiceTask } from '../src/bundles.ts'
import { create, dispose, harness, run } from './harness.ts'

const contexts: Context[] = []
afterEach(async () => { for (const ctx of contexts.splice(0)) await ctx.fiber.dispose() })
async function setup() {
  const value = await harness()
  contexts.push(value.ctx)
  return value
}
const task = 'Start an API server, check the client request, and leave the service running after this task ends.'

describe('final Bole experience through real Loader composition', () => {
  it.each(['qwen3.8-27b', 'Qwen3.8-27B', 'Ornith-1.5-35B-A3B', 'ornith-1.5-35b-a3b'])(
    'delivers only the approved experience for %s and records replayable receipt', async (model) => {
      const { ctx, adapter } = await setup()
      const bundles = await loadBundles()
      const selected = bundles.find(item => item.models.includes(model))!
      const foreign = bundles.find(item => item !== selected)!
      const agent = await create(ctx, model)
      const messages = await run(agent, task)
      const receipt = messages.find(message => message.source.kind === 'bole-experience')
      expect(receipt?.source).toMatchObject({
        status: 'delivered', model, provider: 'test-route', bundleId: selected.id,
        deliveredMemoryIds: [selected.skill.id], bodySha256: selected.skill.sha256,
      })
      expect(JSON.stringify(adapter.requests[0])).toContain(escapeText(selected.skill.body))
      expect(JSON.stringify(adapter.requests[0])).not.toContain(foreign.skill.name)
      const catalog = (await ctx.skills.list({ scope: agent })).filter(isModelInvocable)
      expect(catalog.map(item => item.name)).toEqual([selected.skill.name])
      expect(await ctx.skills.get(selected.skill.name, { scope: agent })).toMatchObject({ content: selected.skill.body })
      expect(await ctx.skills.get(foreign.skill.name, { scope: agent })).toBeUndefined()
      const denied = await ctx.tools.execute({
        name: 'skill', callId: ToolCallId('wrong-model'), arguments: { name: foreign.skill.name },
        agent, signal: new AbortController().signal,
      })
      expect(denied.isError).toBe(true)
      expect(JSON.stringify(denied)).not.toContain(foreign.skill.body)
      expect(Buffer.byteLength(JSON.stringify(receipt), 'utf8')).toBeLessThanOrEqual(4096)
      if (model === selected.models[0]) {
        await expect(`${JSON.stringify({ ...receipt, id: 'experience-message' }, null, 2)}\n`)
          .toMatchFileSnapshot(`./expected/${selected.id}.json`)
      }
      const repeated = await run(agent, task)
      expect(repeated.filter(message => message.source.kind === 'bole-experience')).toHaveLength(1)
    },
  )

  it('keeps Standard free of Bole contexts and candidates while RSI-infer is registered', async () => {
    const { ctx, adapter } = await setup()
    const agent = await create(ctx, 'qwen3.8-27b', 'standard')
    await run(agent, task)
    expect(adapter.requests).toHaveLength(1)
    expect(await ctx.skills.list({ scope: agent })).toEqual([])
    expect(JSON.stringify(adapter.requests)).not.toContain('bole-')
    expect(agent.session.deriveMessages().some(message => message.source.kind === 'bole-experience')).toBe(false)
    expect(await ctx.skills.list()).toEqual([])
  })

  it('offers no experience for unapproved models, including similar Qwen ids', async () => {
    const { ctx, adapter } = await setup()
    const agent = await create(ctx, 'qwen3.8-32b')
    const messages = await run(agent, task)
    expect(messages.find(message => message.source.kind === 'bole-experience')?.source)
      .toMatchObject({ status: 'unsupported-model', deliveredMemoryIds: [], bundleId: null })
    expect((await ctx.skills.list({ scope: agent })).filter(isModelInvocable)).toEqual([])
    expect(JSON.stringify(adapter.requests)).not.toContain('nohup')
  })

  it('defers the body on unrelated tasks, then delivers it once a service request arrives', async () => {
    const { ctx, adapter } = await setup()
    const agent = await create(ctx, 'qwen3.8-27b')
    const first = await run(agent, 'Fix a string parsing bug and run its unit tests.')
    expect(first.find(message => message.source.kind === 'bole-experience')?.source)
      .toMatchObject({ status: 'available', deliveredMemoryIds: [] })
    expect(JSON.stringify(adapter.requests)).not.toContain('PPID 1')
    const later = await run(agent, task)
    expect(later.filter(message => message.source.kind === 'bole-experience')).toHaveLength(2)
    expect(JSON.stringify(adapter.requests.at(-1))).toContain('PPID 1')
  })

  it('checks the final outbound route rather than trusting the skill catalog', async () => {
    const { ctx, adapter } = await setup()
    const agent = await create(ctx, 'qwen3.8-27b')
    agent.ctx.on('agent/request', async (_payload, next) => ({ ...await next(), model: 'Ornith-1.5-35B-A3B' }))
    await expect(run(agent, task)).rejects.toThrow('model-route bound')
    expect(adapter.requests).toEqual([])
    expect(agent.session.deriveMessages().some(message => message.source.kind === 'bole-experience')).toBe(false)
  })

  it('refuses a model switch after a receipt, but permits a fresh Ornith task', async () => {
    const { ctx, adapter } = await setup()
    const agent = await create(ctx, 'qwen3.8-27b')
    const selection: ModelSelectionRef = { current: { provider: 'test-route', model: 'qwen3.8-27b' }, assembled: undefined }
    installModelSelection(agent.ctx, selection)
    await run(agent, task)
    selection.current = { provider: 'test-route', model: 'Ornith-1.5-35B-A3B' }
    await expect(run(agent, 'Continue.')).rejects.toThrow('model-route bound')
    expect(adapter.requests).toHaveLength(1)
    const fresh = await create(ctx, 'Ornith-1.5-35B-A3B')
    await run(fresh, task)
    expect(adapter.requests).toHaveLength(2)
    expect(JSON.stringify(adapter.requests.at(-1))).not.toContain('handoff-v3')
  })

  it('rejects an over-budget complete context without partial delivery', async () => {
    const { ctx, adapter } = await setup()
    await ctx.agentPresets.register({
      id: 'tiny', plugins: [{ name: 'cordis:bole-experience', config: { maxContextBytes: 1 } }],
    })
    const agent = await create(ctx, 'qwen3.8-27b', 'tiny')
    await expect(run(agent, task)).rejects.toThrow('maxContextBytes')
    expect(adapter.requests).toEqual([])
    expect(agent.session.deriveMessages().some(message => message.source.kind === 'bole-experience')).toBe(false)
  })

  it('isolates concurrent Qwen, Ornith and Standard tasks in the same registry', async () => {
    const { ctx, adapter } = await setup()
    const [qwen, ornith, standard] = await Promise.all([
      create(ctx, 'qwen3.8-27b'), create(ctx, 'Ornith-1.5-35B-A3B'), create(ctx, 'qwen3.8-27b', 'standard'),
    ])
    const results = await Promise.all([run(qwen, task), run(ornith, task), run(standard, task)])
    expect(adapter.requests).toHaveLength(3)
    expect(results[0]?.find(message => message.source.kind === 'bole-experience')?.source)
      .toMatchObject({ bundleId: 'qwen38-service-v3' })
    expect(results[1]?.find(message => message.source.kind === 'bole-experience')?.source)
      .toMatchObject({ bundleId: 'ornith-v18' })
    expect(results[2]?.some(message => message.source.kind === 'bole-experience')).toBe(false)
    expect((await ctx.skills.list({ scope: standard })).filter(isModelInvocable)).toEqual([])
    expect(await ctx.skills.get('bole-ornith-persistent-service-verifier-lifecycle-v1', { scope: qwen })).toBeUndefined()
    expect(await ctx.skills.get('bole-qwen38-persistent-service-handoff-v3', { scope: ornith })).toBeUndefined()
  })

  it('keeps the route bound after compaction replaces the visible receipt', async () => {
    const { ctx, adapter } = await setup()
    const agent = await create(ctx, 'qwen3.8-27b')
    const selection: ModelSelectionRef = { current: { provider: 'test-route', model: 'qwen3.8-27b' }, assembled: undefined }
    installModelSelection(agent.ctx, selection)
    await run(agent, task)
    const event = agent.session.snapshotEvents().find(event => event.type === 'user/message' && event.data.source.kind === 'bole-experience')!
    agent.session.append('user/message', createUserMessage({
      content: [{ type: 'text', text: 'The service was started.' }], source: { kind: 'user' },
    }), { surfaceOp: { op: 'replace', startSeq: event.seq, endSeq: event.seq }, sourceEventSeqs: [event.seq] })
    expect(agent.session.deriveMessages().some(message => message.source.kind === 'bole-experience')).toBe(false)
    selection.current = { provider: 'test-route', model: 'Ornith-1.5-35B-A3B' }
    await expect(run(agent, 'Continue.')).rejects.toThrow('model-route bound')
    expect(adapter.requests).toHaveLength(1)
  })

  it('gives a Qwen child its own experience without changing its Ornith parent', async () => {
    const { ctx } = await setup()
    const parent = await create(ctx, 'Ornith-1.5-35B-A3B')
    await run(parent, task)
    const child = await create(ctx, 'qwen3.8-27b', 'rsi-infer', 'test-route', parent)
    const messages = await run(child, task)
    expect(messages.find(message => message.source.kind === 'bole-experience')?.source)
      .toMatchObject({ bundleId: 'qwen38-service-v3' })
    expect(await ctx.skills.get('bole-ornith-persistent-service-verifier-lifecycle-v1', { scope: child })).toBeUndefined()
    await dispose(child)
    expect((await ctx.skills.list({ scope: parent })).filter(isModelInvocable).map(skill => skill.name))
      .toEqual(['bole-ornith-persistent-service-verifier-lifecycle-v1'])
  })

  it('enforces exact and one-byte-too-small complete message bounds with multibyte route metadata', async () => {
    const { ctx, adapter } = await setup()
    const sample = await run(await create(ctx, 'qwen3.8-27b', 'rsi-infer', '测试路由'), task)
    const receipt = sample.find(message => message.source.kind === 'bole-experience')!
    const bytes = Buffer.byteLength(JSON.stringify(receipt), 'utf8')
    expect(bytes).toBeGreaterThan(JSON.stringify(receipt).length)
    for (const [mode, bound] of [['exact', bytes], ['short', bytes - 1]] as const) {
      await ctx.agentPresets.register({
        id: mode, plugins: [{ name: 'cordis:bole-experience', config: { maxContextBytes: bound } }],
      })
    }
    await run(await create(ctx, 'qwen3.8-27b', 'exact', '测试路由'), task)
    const short = await create(ctx, 'qwen3.8-27b', 'short', '测试路由')
    await expect(run(short, task)).rejects.toThrow('maxContextBytes')
    expect(adapter.requests).toHaveLength(2)
    expect(short.session.deriveMessages().some(message => message.source.kind === 'bole-experience')).toBe(false)
  })

  it('removes the exact provider when the Agent or contributing preset unloads', async () => {
    const { ctx } = await setup()
    const agent = await create(ctx, 'qwen3.8-27b', 'standard')
    const pluginFiber = await agent.ctx.plugin(plugin, { maxContextBytes: 4096 })
    await run(agent, task)
    expect((await ctx.skills.list({ scope: agent })).filter(isModelInvocable)).toHaveLength(1)
    await pluginFiber.dispose()
    expect(await ctx.skills.list({ scope: agent })).toEqual([])
    // Receipts are attribution, not a required replay projection. A reader
    // without the producer must retain the complete JSON and message body.
    const receipt = agent.session.deriveMessages().find(message => message.source.kind === 'bole-experience')!
    const archive = Session.create(agent.session.id)
    archive.append('user/message', receipt, { surfaceOp: 'append' })
    const seed = JSON.parse(JSON.stringify(archive.snapshotEvents())) as SessionEvent[]
    const restored = Session.fromRestore(archive.id, seed, archive.header, SessionLogOffset(0), 'detached')
    expect(restored.deriveMessages()).toEqual([receipt])
    expect(restored.snapshotEvents()[0]?.data).toEqual(receipt)
    const other = await create(ctx, 'Ornith-1.5-35B-A3B')
    await run(other, task)
    await dispose(other)
    expect(await ctx.skills.list({ scope: other })).toEqual([])
    await dispose(agent)
    expect(await ctx.skills.list({ scope: agent })).toEqual([])
  })

  it('keeps the function-plugin namespace intact through Loader unwrapping', async () => {
    const { ctx } = await setup()
    expect('default' in plugin).toBe(false)
    expect(ctx.loader.unwrapExports(plugin)).toBe(plugin)
    expect(plugin.inject).toEqual(['agents', 'skills', 'systemPrompt'])
  })
})

describe('public-task routing and immutable selection', () => {
  it.each([
    ['Start a server and leave it running.', true], ['启动服务并在任务结束后保持运行。', true],
    ['Fix the API parser tests.', false], ['Compare model scores.', false],
  ])('routes %s without benchmark ids', (text, expected) => {
    expect(matchesServiceTask(text)).toBe(expected)
  })
  it('pins the user-selected full-run scores without treating them as current deployment gains', async () => {
    expect((await loadBundles()).map(bundle => [bundle.id, bundle.evaluation.passed, bundle.evaluation.total]))
      .toEqual([['qwen38-service-v3', 63, 89], ['ornith-v18', 66, 89]])
  })
})
