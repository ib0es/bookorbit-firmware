import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import ts from '../../web-flasher/node_modules/typescript/lib/typescript.js';

const source = readFileSync(new URL('../../web-flasher/src/services/fontPartition.ts', import.meta.url), 'utf8');
const js = ts.transpileModule(source, { compilerOptions: { module: ts.ModuleKind.ESNext, target: ts.ScriptTarget.ES2020 } }).outputText.replace('import.meta.env.BASE_URL', JSON.stringify('./'));
const { checkFontPartition, loadFontFamilyImage, parseInstalledFont, buildFontPartitionImage, sameFontImage } = await import('data:text/javascript;base64,' + Buffer.from(js).toString('base64'));
const partition = { name: 'spiffs', type: 1, subtype: 0x82, offset: 0x20000, size: 0x2000 };

test('Flash capacity accounts for the header and sector erasure at the exact boundary', () => {
  assert.equal(checkFontPartition([partition], 0x40000, 8192), partition);
  assert.throws(() => checkFontPartition([partition], 0x40000, 8193), /fit/);
});

test('unsafe layouts never allow a font write', () => {
  for (const p of [ { ...partition, offset: 0x20001 }, { ...partition, size: 8193 }, { ...partition, offset: 0x1000 }, { ...partition, subtype: 2 } ]) {
    assert.throws(() => checkFontPartition([p], 0x40000, 4097));
  }
  assert.throws(() => checkFontPartition([partition], 0x21000, 4097));
  assert.throws(() => checkFontPartition([partition], 0, 4097));
  assert.throws(() => checkFontPartition([partition, { ...partition, name: 'app', type: 0, offset: 0x21000 }], 0x40000, 4097), /overlap/);
});

test('font faces are placed by style even when archive manifest order is alphabetical', async () => {
  const original = globalThis.fetch;
  const values = { 'Test-Bold.ttf': 2, 'Test-BoldItalic.ttf': 4, 'Test-Italic.ttf': 3, 'Test-Regular.ttf': 1 };
  globalThis.fetch = async url => ({ ok: true, arrayBuffer: async () => new Uint8Array([values[url.split('/').pop()]]).buffer });
  try {
    const image = await loadFontFamilyImage({ name: 'Test', dir: 'Test', files: Object.keys(values) });
    const header = new DataView(image.buffer);
    assert.equal(parseInstalledFont(image, 8192), 'Test');
    for (let style = 0; style < 4; style++) assert.equal(image[header.getUint32(72 + style * 8, true)], style + 1);
  } finally { globalThis.fetch = original; }
});

test('already-installed comparison ignores reserved padding but detects changed font data', () => {
  const expected = buildFontPartitionImage('Test', [new Uint8Array([1, 2, 3, 4])]);
  const actual = expected.slice();
  actual.fill(255, 108, 4096);
  assert.equal(sameFontImage(expected, actual), true);
  actual[4096] ^= 1;
  assert.equal(sameFontImage(expected, actual), false);
});
