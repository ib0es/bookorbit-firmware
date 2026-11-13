import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import ts from '../../web-flasher/node_modules/typescript/lib/typescript.js';
const transpile = (name) => ts.transpileModule(readFileSync(new URL(`../../web-flasher/src/services/${name}.ts`, import.meta.url), 'utf8'), { compilerOptions: { module: ts.ModuleKind.ESNext, target: ts.ScriptTarget.ES2020 } }).outputText;
const moduleUrl = source => 'data:text/javascript;base64,' + Buffer.from(source).toString('base64');
const hashModule = moduleUrl('export const md5Hex = bytes => globalThis.__flashReadMd5(bytes);');
const { readFlash } = await import(moduleUrl(transpile('flashRead').replace("'./firmwareImage'", JSON.stringify(hashModule))));
const { inspectReaderSession } = await import(moduleUrl(transpile('readerSession')));
globalThis.__flashReadMd5 = bytes => createHash('md5').update(bytes).digest('hex');

function loaderFixture(bytes, { timeout = false, corruptDigest = false, shortAt = -1 } = {}) {
  const packets = [], commands = [], acknowledgements = [];
  let detections = 0;
  const loader = {
    detectFlashSize: async () => { detections++; return '16MB'; },
    checkCommand: async (description, op, payload) => {
      commands.push({ description, op, words: [...new Uint32Array(payload.buffer)] });
      if (op === 0xd2) {
        const view = new DataView(payload.buffer);
        const start = view.getUint32(0, true) - 0x8000;
        const length = view.getUint32(4, true);
        const chunk = bytes.subarray(start, start + length);
        const requestIndex = commands.filter(c => c.op === 0xd2).length - 1;
        packets.push(requestIndex === shortAt ? chunk.subarray(0, chunk.length - 16) : chunk);
        packets.push(new Uint8Array(corruptDigest ? 16 : createHash('md5').update(chunk).digest()));
      }
    },
    transport: {
      read: async () => { if (timeout) throw new Error('No serial data received.'); return packets.shift(); },
      write: async bytes => acknowledgements.push(new DataView(bytes.buffer).getUint32(0, true)),
    },
  };
  return { loader, commands, acknowledgements, detections: () => detections };
}

test('Flash reads use independent 256-byte requests and verify every digest', async () => {
  const bytes = new Uint8Array(3072).map((_, i) => i % 255);
  const fixture = loaderFixture(bytes);
  assert.deepEqual(await readFlash(fixture.loader, 0x8000, bytes.length), bytes);
  assert.deepEqual(fixture.commands[1].words, [0x8000, 256, 256, 1]);
  assert.deepEqual(fixture.commands[12].words, [0x8b00, 256, 256, 1]);
  assert.deepEqual(fixture.acknowledgements, new Array(12).fill(256));
  await readFlash(fixture.loader, 0x8000, bytes.length);
  assert.equal(fixture.detections(), 1);
  assert.equal(fixture.commands.filter(c => c.op === 0xb).length, 2);
  assert.equal(fixture.commands.filter(c => c.op === 0xd2).length, 24);
});

test('USB timeouts and MD5 mismatches remain errors', async () => {
  for (const options of [{ timeout: true }, { corruptDigest: true }]) {
    const { loader } = loaderFixture(new Uint8Array(1024), options);
    await assert.rejects(readFlash(loader, 0x8000, 1024), /serial|MD5/);
  }
});

test('inspection recovers from timeout only after closing and reopening the reader', async () => {
  const events = [];
  let sessions = 0;
  const result = await inspectReaderSession(
    async () => { events.push('open'); return ++sessions; },
    async session => { events.push(`inspect ${session}`); if (session === 1) throw new Error('No serial data'); return 'Book Orbit'; },
    async () => { events.push('close'); },
    () => { events.push('retry'); },
  );
  assert.equal(result.result, 'Book Orbit');
  assert.deepEqual(events, ['open', 'inspect 1', 'close', 'retry', 'open', 'inspect 2']);
});

test('persistent USB failure closes both sessions and preserves its actual cause', async () => {
  let closes = 0, opens = 0;
  await assert.rejects(inspectReaderSession(
    async () => ++opens,
    async () => { throw new Error('No serial data received.'); },
    async () => { closes++; },
    () => {},
  ), /No serial data/);
  assert.equal(opens, 2); assert.equal(closes, 2);
});

const { inspectFlashState } = await import(moduleUrl(transpile('readerSession')));
test('optional font read failure preserves a successful firmware detection without retrying', async () => {
  let scans = 0;
  const result = await inspectFlashState(
    async () => ({ isBookOrbit: true, board: 'x4' }),
    async () => { scans++; throw new Error('No serial data received.'); }, true,
  );
  assert.equal(result.signature.isBookOrbit, true);
  assert.equal(result.firmwareInspected, true);
  assert.equal(result.installedFont.status, 'unknown');
  assert.match(result.inspectionError, /No serial data/);
  assert.equal(scans, 1);
});

test('failed firmware read remains unknown, rather than claiming factory firmware', async () => {
  let fontReads = 0;
  const result = await inspectFlashState(
    async () => { throw new Error('USB timeout'); },
    async () => { fontReads++; return { status: 'empty' }; }, true,
  );
  assert.equal(result.firmwareInspected, false);
  assert.equal(fontReads, 0);
  assert.equal(result.inspectionError, 'USB timeout');
});

test('font write preflight stays strict when optional connect inspection is permitted', async () => {
  await assert.rejects(inspectFlashState(
    async () => ({ isBookOrbit: true, board: 'x4' }),
    async () => { throw new Error('USB timeout'); }, false,
  ), /USB timeout/);
});

test('a short USB packet aborts the read and is never acknowledged as valid data',async()=>{
  const f=loaderFixture(new Uint8Array(3072),{shortAt:3});
  await assert.rejects(readFlash(f.loader,0x8000,3072),/data packet after 768\/3072 bytes.*packet of 240 bytes, expected 256/);
  assert.equal(f.acknowledgements.length,3);
  assert.equal(f.commands.filter(c=>c.op===0xd2).length,4);
});

test('catalog records use a short final transaction without reading padding',async()=>{
  const bytes=new Uint8Array(276).fill(77),f=loaderFixture(bytes);
  assert.deepEqual(await readFlash(f.loader,0x8000,276),bytes);
  assert.deepEqual(f.commands.filter(c=>c.op===0xd2).map(c=>c.words),[[0x8000,256,256,1],[0x8100,20,256,1]]);
});
