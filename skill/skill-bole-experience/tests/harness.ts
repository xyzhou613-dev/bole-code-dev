/** Loader-backed production loop and preset registry; the model adapter is the only stub. */
import { randomUUID } from 'node:crypto'
import { Context } from '@deepseek-ai/cordis'
import Loader from '@deepseek-ai/cordis-plugin-loader'
import Include from '@deepseek-ai/cordis-plugin-include'
import * as llm from '@deepseek-ai/dsh-llm'
import * as sessions from '@deepseek-ai/dsh-session'
import * as projections from '@deepseek-ai/dsh-session-projection'
import * as prompt from '@deepseek-ai/dsh-system-prompt'
import * as tools from '@deepseek-ai/dsh-tools'
import * as skills from '@deepseek-ai/dsh-skill'
import * as agents from '@deepseek-ai/dsh-agent'
import * as loop from '@deepseek-ai/dsh-agent-loop'
import * as presets from '@deepseek-ai/dsh-agent-preset-registry'
import * as preset from '@deepseek-ai/dsh-agent-preset'
import * as toolSkill from '@deepseek-ai/dsh-tool-skill'
import * as experience from '../src/index.ts'

const handles = new WeakMap<agents.Agent, agents.AgentHandle>()

/** Scripted boundary retains the actual requests produced by the loop. */
export class RecordingAdapter extends llm.LlmAdapter {
  readonly requests: llm.GenerateOptions[] = []
  override async* stream(request: llm.GenerateOptions): AsyncIterable<llm.StreamChunk> {
    this.requests.push(request)
    yield { type: 'block-start', index: 0, blockType: 'text' }
    yield { type: 'block-end', index: 0, block: { type: 'text', text: 'done' } }
    yield { type: 'finish', reason: { kind: 'stop' } }
  }
}

/** Start the test-only Cordis composition via Loader with real namespace unwrapping. */
export async function harness() {
  const ctx = new Context()
  ctx.baseUrl = new URL('./fixtures/', import.meta.url).href
  await ctx.plugin(Loader)
  Object.assign(ctx.loader.builtins, {
    include: Include, llm, session: sessions, projections, 'system-prompt': prompt,
    tools, skills, agents, loop, presets, preset, 'tool-skill': toolSkill, 'bole-experience': experience,
  })
  await ctx.loader.create({ name: 'cordis:include', config: { path: new URL('./fixtures/cordis.yml', import.meta.url).href } })
  await ctx.loader.await()
  const adapter = new RecordingAdapter()
  ctx.llm.registerAdapter(['test-route', '测试路由'], adapter)
  return { ctx, adapter }
}

/** Create a real scoped Agent from one of the declared presets. */
export async function create(ctx: Context, model: string, mode = 'rsi-infer', provider = 'test-route', parentAgent?: agents.Agent) {
  const handle = await ctx.agents.create({
    sessionId: sessions.SessionId(randomUUID()),
    agentOptions: { provider, model },
    ...parentAgent === undefined ? {} : { parentAgent },
    setup: async (agentCtx) => { await ctx.agentPresets.mount(agentCtx, mode) },
  })
  handles.set(handle.agent, handle)
  return handle.agent
}

/** Dispose an Agent through the factory's public handle and await quiescence. */
export async function dispose(agent: agents.Agent): Promise<void> {
  await handles.get(agent)?.dispose()
}

/** Drive one user turn to idle and return its durable messages. */
export async function run(agent: agents.Agent, text: string) {
  const failures: unknown[] = []
  const off = agent.ctx.on('agent/error', ({ agent: subject, error }) => { if (subject === agent) failures.push(error) })
  try {
    agent.followup(llm.createUserMessage({ content: [{ type: 'text', text }], source: { kind: 'user' } }))
    await agent.whenIdle()
    if (failures.length > 0) throw failures[0]
    return agent.session.deriveMessages()
  } finally {
    off()
  }
}
