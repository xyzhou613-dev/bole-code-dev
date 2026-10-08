/** .docx text extraction and native write-back survive authorization, version, and interpreter failures. */
import { EventEmitter } from 'node:events'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { PassThrough } from 'node:stream'
import { Context } from '@deepseek-ai/cordis'
import { SessionId } from '@deepseek-ai/dsh-session/types'
import type { WorkspaceFiles } from '@deepseek-ai/dsh-api-workspace-files'
import type { FileSystem, FsInfo, FsTarget } from '@deepseek-ai/dsh-fs'
import { OfficeToPdfError } from '../src/index.ts'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import OfficeToPdf from '../src/index.ts'

/** Scripted `node:child_process` spawn, so the real launcher's success and retry paths run without a host interpreter. */
const childProcessMocks = vi.hoisted(() => ({ spawn: vi.fn() }))

vi.mock('node:child_process', async (importOriginal) => {
  const actual = await importOriginal<typeof import('node:child_process')>()
  return { ...actual, spawn: childProcessMocks.spawn as typeof actual.spawn }
})

/** A child `launchWithFirstAvailable` drives through its streams and lifecycle events. */
class FakeChild extends EventEmitter {
  readonly stdout = new PassThrough()
  readonly stderr = new PassThrough()
  readonly stdin = new PassThrough()
  readonly kills: NodeJS.Signals[] = []
  kill(signal: NodeJS.Signals): boolean { this.kills.push(signal); return true }
  /** Finish successfully, forwarding `stdout` data before the close event. */
  succeed(data: string): void { this.stdout.write(Buffer.from(data)); this.stdout.end(); this.emit('close', 0) }
  /** Finish unsuccessfully, forwarding `stderr` data before a nonzero close. */
  fail(code: number, data = 'boom'): void { this.stderr.write(Buffer.from(data)); this.stderr.end(); this.emit('close', code) }
  /** Close nonzero without any `stderr`, forcing the exit-code fallback message. */
  exit(code: number): void { this.emit('close', code) }
  /** Abandon before a close, standing in for a launch failure. */
  explode(): void { this.emit('error', new Error('spawn failed')) }
}
/** Queue one child action behind the launcher's synchronous listener wiring. */
function enqueue(fn: () => void): void { setImmediate(fn) }

const scope = { sessionId: SessionId('document-test'), workspaceRoot: '/workspace' }
const source = { absolutePath: '/workspace/report.docx', version: 'source-v1', bytes: 4 }
const rawSource = { ...source, data: new Uint8Array([80, 75, 3, 4]) }
const wireSource = { ...source, offset: 0, eof: true, data: rawSource.data }
let ctx: Context
const target = { targetKey: 'office-source' as FsTarget['targetKey'], displayPath: source.absolutePath }
const sourceInfo = { type: 'file' as const, version: source.version as FsInfo['version'], size: source.bytes }
let read: ReturnType<typeof vi.fn<FileSystem['readBytes']>>
let fileInfo: ReturnType<typeof vi.fn<FileSystem['stat']>>
let processPath: ReturnType<typeof vi.fn<FileSystem['processPath']>>
let authorize: ReturnType<typeof vi.fn<WorkspaceFiles['readBytes']>>
let metadata: ReturnType<typeof vi.fn<WorkspaceFiles['stat']>>

beforeEach(async () => {
  childProcessMocks.spawn.mockReset()
  ctx = new Context()
  read = vi.fn<FileSystem['readBytes']>().mockResolvedValue(rawSource.data)
  fileInfo = vi.fn<FileSystem['stat']>().mockResolvedValue(sourceInfo)
  processPath = vi.fn<FileSystem['processPath']>().mockReturnValue(source.absolutePath)
  ctx.provide('fs', {
    resolve: vi.fn().mockResolvedValue(target), stat: fileInfo, processPath, readBytes: read,
  } as never)
  authorize = vi.fn<WorkspaceFiles['readBytes']>().mockResolvedValue({ ...wireSource, eof: false, data: rawSource.data.subarray(0, 1) })
  metadata = vi.fn<WorkspaceFiles['stat']>().mockResolvedValue(source)
  ctx.provide('workspaceFiles', { stat: metadata, readBytes: authorize } as never)
  await ctx.plugin(OfficeToPdf)
})
afterEach(async () => { await ctx.fiber.dispose() })

it('extracts .docx body text under source identity and version', async () => {
  const extract = vi.spyOn(ctx.officeToPdf, 'runPython').mockResolvedValue('{"text":"first\\nsecond"}')
  await expect(ctx.officeToPdf.readText(scope, 'report.docx', new AbortController().signal))
    .resolves.toEqual({ text: 'first\nsecond', version: source.version })
  expect(authorize).toHaveBeenCalledExactlyOnceWith(scope, 'report.docx', { range: { offset: 0, length: 1 } }, expect.any(AbortSignal))
  expect(metadata).toHaveBeenCalledOnce()
  expect(extract).toHaveBeenCalledOnce()
  expect(extract.mock.calls[0]![1]).toBe('')
})

it('rejects a non-docx path before reading any source', async () => {
  await expect(ctx.officeToPdf.readText(scope, 'report.doc', new AbortController().signal))
    .rejects.toMatchObject({ code: 'document-edit/failed', details: { reason: 'unsupported-format' } })
  expect(read).not.toHaveBeenCalled()
  expect(metadata).not.toHaveBeenCalled()
})

it('classifies a changed source during text read before buffering body bytes', async () => {
  metadata.mockResolvedValueOnce({ ...source, version: 'v2' })
  await expect(ctx.officeToPdf.readText(scope, 'report.docx', new AbortController().signal))
    .rejects.toMatchObject({ code: 'document-edit/failed', details: { reason: 'source-changed' } })
  expect(read).not.toHaveBeenCalled()
})

it('classifies invalid extraction output instead of trusting interpreter bytes', async () => {
  vi.spyOn(ctx.officeToPdf, 'runPython').mockResolvedValue('not json')
  await expect(ctx.officeToPdf.readText(scope, 'report.docx', new AbortController().signal))
    .rejects.toMatchObject({ code: 'document-edit/failed', details: { reason: 'invalid-document' } })
})

it('writes body text back to the process path and reports the new version', async () => {
  const write = vi.spyOn(ctx.officeToPdf, 'runPython').mockResolvedValue('')
  processPath.mockReturnValue(join(tmpdir(), 'dsh-office-text-write.docx'))
  const watched = vi.fn()
  ctx.on('fs/observed', (observedTarget, observation) => { watched(observedTarget, observation) })
  fileInfo.mockReset()
  fileInfo.mockResolvedValueOnce(sourceInfo)
  fileInfo.mockResolvedValueOnce({ ...sourceInfo, version: 'source-v2' as FsInfo['version'] })
  const result = await ctx.officeToPdf.saveText(scope, 'report.docx', 'edited\nbody', source.version, new AbortController().signal)
  expect(result).toEqual({ version: 'source-v2' })
  expect(write).toHaveBeenCalledExactlyOnceWith(
    [expect.stringContaining('write_text.py'), expect.stringContaining('.docx')], 'edited\nbody', expect.any(AbortSignal))
  expect(watched).toHaveBeenCalledExactlyOnceWith(target, { kind: 'present', version: 'source-v2' })
  expect(read).toHaveBeenCalledOnce()
  expect(authorize).not.toHaveBeenCalled()
})

it('refuses a save when the loaded version no longer matches the source', async () => {
  metadata.mockResolvedValueOnce({ ...source, version: 'v2' })
  const write = vi.spyOn(ctx.officeToPdf, 'runPython').mockResolvedValue('')
  await expect(ctx.officeToPdf.saveText(scope, 'report.docx', 'text', 'stale', new AbortController().signal))
    .rejects.toMatchObject({ code: 'document-edit/failed', details: { reason: 'source-changed' } })
  expect(write).not.toHaveBeenCalled()
})

it('reports a missing python interpreter as unavailable text editing', async () => {
  const unavailable = new OfficeToPdfError('unavailable', 'No usable python interpreter was found for Office text editing.')
  vi.spyOn(ctx.officeToPdf, 'runPython').mockRejectedValueOnce(unavailable)
  await expect(ctx.officeToPdf.readText(scope, 'report.docx', new AbortController().signal))
    .rejects.toMatchObject({ code: 'document-edit/failed', details: { reason: 'unavailable' } })
})

it.each(['workspaceFiles', 'fs'] as const)('reports unavailable when %s is absent', async (missing) => {
  const independent = new Context()
  try {
    await independent.plugin(OfficeToPdf)
    if (missing === 'workspaceFiles') independent.provide('fs', {} as never)
    else independent.provide('workspaceFiles', {} as never)
    await expect(independent.officeToPdf.readText(scope, 'report.docx', new AbortController().signal))
      .rejects.toMatchObject({ code: 'document-edit/failed', details: { reason: 'unavailable' } })
  } finally { await independent.fiber.dispose() }
})

it('cancels a text read before starting the interpreter', async () => {
  await expect(ctx.officeToPdf.readText(scope, 'report.docx', AbortSignal.abort()))
    .rejects.toMatchObject({ code: 'gateway/cancelled' })
  expect(read).not.toHaveBeenCalled()
  expect(authorize).not.toHaveBeenCalled()
})

it('preserves a non-classified interpreter failure instead of wrapping it as Office failure', async () => {
  const failure = new Error('interpreter denied')
  vi.spyOn(ctx.officeToPdf, 'runPython').mockRejectedValueOnce(failure)
  await expect(ctx.officeToPdf.saveText(scope, 'report.docx', 'text', source.version, new AbortController().signal)).rejects.toBe(failure)
})

it('rejects a source that stops being a file before writing', async () => {
  fileInfo.mockResolvedValueOnce({ ...sourceInfo, type: 'directory' as const })
  const write = vi.spyOn(ctx.officeToPdf, 'runPython').mockResolvedValue('')
  await expect(ctx.officeToPdf.saveText(scope, 'report.docx', 'text', source.version, new AbortController().signal))
    .rejects.toMatchObject({ code: 'document-edit/failed', details: { reason: 'source-changed' } })
  expect(write).not.toHaveBeenCalled()
})

it('drives the real interpreter runner to extract stdout from a clean child', async () => {
  childProcessMocks.spawn.mockImplementation((...args: unknown[]) => {
    expect(args[0]).toBe('python3')
    const child = new FakeChild()
    enqueue(() => { child.succeed('{"text":"first\\nsecond"}') })
    return child
  })
  await expect(ctx.officeToPdf.readText(scope, 'report.docx', new AbortController().signal))
    .resolves.toEqual({ text: 'first\nsecond', version: source.version })
  expect(childProcessMocks.spawn).toHaveBeenCalledOnce()
})

it('falls back to the next interpreter when the first fails to start', async () => {
  childProcessMocks.spawn
    .mockImplementationOnce(() => { const child = new FakeChild(); enqueue(() => { child.explode() }); return child })
    .mockImplementationOnce((...args: unknown[]) => {
      expect(args[0]).toBe('python')
      const child = new FakeChild(); enqueue(() => { child.succeed('{"text":"recovered"}') })
      return child
    })
  await expect(ctx.officeToPdf.readText(scope, 'report.docx', new AbortController().signal))
    .resolves.toEqual({ text: 'recovered', version: source.version })
  expect(childProcessMocks.spawn).toHaveBeenCalledTimes(2)
})

it('surfaces a script failure instead of falling back or reporting unavailable', async () => {
  childProcessMocks.spawn.mockImplementationOnce(() => {
    const child = new FakeChild(); enqueue(() => { child.fail(2, 'write_text.py crashed') }); return child
  })
  await expect(ctx.officeToPdf.readText(scope, 'report.docx', new AbortController().signal))
    .rejects.toThrow('write_text.py crashed')
  expect(childProcessMocks.spawn).toHaveBeenCalledTimes(1)
})

it('wraps a write script failure so the client surfaces its stderr instead of generic unavailable', async () => {
  childProcessMocks.spawn.mockImplementationOnce(() => {
    const child = new FakeChild(); enqueue(() => { child.fail(3, 'zipfile failed to open') }); return child
  })
  await expect(ctx.officeToPdf.saveText(scope, 'report.docx', 'text', source.version, new AbortController().signal))
    .rejects.toMatchObject({ code: 'document-edit/failed', details: { reason: 'failed' } })
  expect(childProcessMocks.spawn).toHaveBeenCalledTimes(1)
})

it('kills a running child and rethrows when the launch is cancelled mid-flight', async () => {
  const controller = new AbortController()
  let child: FakeChild | undefined
  childProcessMocks.spawn.mockImplementation((...args: unknown[]) => {
    expect(args[0]).toBe('python3')
    child = new FakeChild()
    enqueue(() => { controller.abort(); child?.exit(2) })
    return child
  })
  await expect(ctx.officeToPdf.readText(scope, 'report.docx', controller.signal))
    .rejects.toMatchObject({ code: 'gateway/cancelled' })
  expect(child?.kills).toHaveLength(1)
})

it('maps a too-large native read to an input-too-large text error', async () => {
  read.mockRejectedValueOnce({ code: 'FS_TOO_LARGE' })
  await expect(ctx.officeToPdf.readText(scope, 'report.docx', new AbortController().signal))
    .rejects.toMatchObject({ code: 'document-edit/failed', details: { reason: 'input-too-large' } })
})

it('classifies a non-string extracted payload as an invalid document', async () => {
  vi.spyOn(ctx.officeToPdf, 'runPython').mockResolvedValue('{"text":42}')
  await expect(ctx.officeToPdf.readText(scope, 'report.docx', new AbortController().signal))
    .rejects.toMatchObject({ code: 'document-edit/failed', details: { reason: 'invalid-document' } })
})

it('preserves an unrelated native read error from readText instead of classifying it', async () => {
  const failure = new Error('storage offline')
  read.mockRejectedValueOnce(failure)
  await expect(ctx.officeToPdf.readText(scope, 'report.docx', new AbortController().signal)).rejects.toBe(failure)
})

it('rejects a non-docx write before reading any source', async () => {
  await expect(ctx.officeToPdf.saveText(scope, 'report.doc', 'text', source.version, new AbortController().signal))
    .rejects.toMatchObject({ code: 'document-edit/failed', details: { reason: 'unsupported-format' } })
  expect(read).not.toHaveBeenCalled()
  expect(metadata).not.toHaveBeenCalled()
})

it.each(['workspaceFiles', 'fs'] as const)('reports unavailable writing when %s is absent', async (missing) => {
  const independent = new Context()
  try {
    await independent.plugin(OfficeToPdf)
    if (missing === 'workspaceFiles') independent.provide('fs', {} as never)
    else independent.provide('workspaceFiles', {} as never)
    await expect(independent.officeToPdf.saveText(scope, 'report.docx', 'text', source.version, new AbortController().signal))
      .rejects.toMatchObject({ code: 'document-edit/failed', details: { reason: 'unavailable' } })
  } finally { await independent.fiber.dispose() }
})

it('cancels a text write before running any interpreter', async () => {
  await expect(ctx.officeToPdf.saveText(scope, 'report.docx', 'text', source.version, AbortSignal.abort()))
    .rejects.toMatchObject({ code: 'gateway/cancelled' })
  expect(read).not.toHaveBeenCalled()
})

it('reports unavailable when the source disappears after writing', async () => {
  fileInfo.mockReset()
  fileInfo.mockResolvedValueOnce(sourceInfo)
  fileInfo.mockResolvedValueOnce(undefined)
  processPath.mockReturnValue(join(tmpdir(), 'dsh-office-text-write.docx'))
  vi.spyOn(ctx.officeToPdf, 'runPython').mockResolvedValue('')
  await expect(ctx.officeToPdf.saveText(scope, 'report.docx', 'text', source.version, new AbortController().signal))
    .rejects.toMatchObject({ code: 'document-edit/failed', details: { reason: 'unavailable' } })
})

it('rejects a text read when the resolved source stops being a file', async () => {
  fileInfo.mockResolvedValueOnce({ ...sourceInfo, type: 'directory' as const })
  await expect(ctx.officeToPdf.readText(scope, 'report.docx', new AbortController().signal))
    .rejects.toMatchObject({ code: 'document-edit/failed', details: { reason: 'source-changed' } })
})
