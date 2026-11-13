// Run with: node --test test/web/test_web_config_fonts.mjs
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import test from 'node:test';

const source = readFileSync(new URL('../../src/net/web_config.cpp', import.meta.url), 'utf8');
const script = source.match(/<script>([\s\S]*?)<\/script>/)[1];

function portal(outcomes = [], fontState = {}) {
  const elements = new Map();
  function element(id) {
    if (!elements.has(id)) elements.set(id, {
      value: '', textContent: '', className: '', disabled: false, checked: false,
      files: [], style: {}, options: [{ value: '' }], children: [],
      addEventListener() {}, appendChild(child) { this.children.push(child); },
      remove(index) { this.options.splice(index, 1); },
      add(option) { this.options.push(option); },
    });
    return elements.get(id);
  }
  element('cfg-form').elements = { namedItem: () => element('font-family') };
  const requests = [];
  const families = fontState.families ?? [{ name: 'Literata', bytes: 1000, fits: true }];
  const fetchCalls = [];
  class XHR {
    upload = {};
    open(method, url) { this.url = url; }
    send(data) {
      requests.push({ url: this.url, data });
      const result = outcomes.shift() ?? { ok: true };
      this.status = 200;
      this.responseText = JSON.stringify({ files: [result] });
      queueMicrotask(() => this.onload());
    }
  }
  const context = vm.createContext({
    document: { getElementById: element, createElement: () => ({}) },
    XMLHttpRequest: XHR,
    FormData: class { append(...args) { this.args = args; } },
    Option: class { constructor(text, value) { this.text = text; this.value = value; } },
    fetch: async (url, options) => {
      fetchCalls.push({ url, options });
      return { ok: !fontState.failInstall || url !== '/api/fonts/install', json: async () => url === '/api/fonts'
        ? { capacity: 8192, free: 4096, used: 4096, families, installedFamilies: fontState.installedFamilies ?? ((fontState.installed ?? 'Noto Sans') ? [{ name: fontState.installed ?? 'Noto Sans', bytes: 1000 }] : []), builtin: 'Noto Serif' }
        : (url === '/api/fonts/install' || url === '/api/fonts/flash/delete') ? (fontState.failInstall ? { ok: false, error: 'Does not fit' } : { ok: true }) : { books: [] } };
    },
    setTimeout, URLSearchParams, File, Blob, TextDecoder, DecompressionStream,
  });
  vm.runInContext(script, context);
  return { context, element, requests, fetchCalls };
}

test('font batch uses font endpoint and refreshes the settings family selector', async () => {
  const { context, element, requests } = portal();
  element('font-input').files = [
    { name: 'Literata-Regular.ttf', size: 1000 },
    { name: 'Literata-Bold.ttf', size: 900 },
  ];
  element('font-overwrite').checked = true;
  await context.uploadFonts();
  await context.loadFonts();
  assert.equal(requests.length, 2);
  assert.equal(requests[0].url, '/api/upload?kind=font&overwrite=1&sizes=1000');
  assert.equal(requests[1].url, '/api/upload?kind=font&overwrite=1&sizes=900');
  assert.equal(element('font-progress').value, 100);
  assert.equal(element('font-up').disabled, false);
  assert.deepEqual(element('font-family').options.map(o => o.value), ['', 'Literata', 'Noto Sans']);
});

test('rejected upload stops the batch and restores the controls', async () => {
  const { context, element, requests } = portal([{ ok: false, error: 'Invalid TTF/OTF font' }]);
  element('font-input').files = [{ name: 'bad.ttf', size: 12 }, { name: 'good.ttf', size: 1000 }];
  await context.uploadFonts();
  assert.equal(requests.length, 1);
  assert.match(element('toast').textContent, /bad.ttf: Invalid TTF\/OTF font/);
  assert.equal(element('btn-save').disabled, false);
  assert.equal(element('btn-close').disabled, false);
  assert.equal(element('font-input').disabled, false);
});

test('fonts are not sent while a book upload is active', async () => {
  const { context, element, requests } = portal();
  element('font-input').files = [{ name: 'font.ttf', size: 1000 }];
  context.uploading = true;
  await context.uploadFonts();
  assert.equal(requests.length, 0);
});

function storedZip(name = 'fonts/Test/Test-Regular.ttf') {
  const bytes = Buffer.from('123456789');
  const path = Buffer.from(name);
  const local = Buffer.alloc(30);
  local.writeUInt32LE(0x04034b50);
  local.writeUInt32LE(0xcbf43926, 14);
  local.writeUInt32LE(bytes.length, 18);
  local.writeUInt32LE(bytes.length, 22);
  local.writeUInt16LE(path.length, 26);
  const central = Buffer.alloc(46);
  central.writeUInt32LE(0x02014b50);
  central.writeUInt32LE(0xcbf43926, 16);
  central.writeUInt32LE(bytes.length, 20);
  central.writeUInt32LE(bytes.length, 24);
  central.writeUInt16LE(path.length, 28);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50);
  end.writeUInt16LE(1, 8); end.writeUInt16LE(1, 10);
  end.writeUInt32LE(central.length + path.length, 12);
  end.writeUInt32LE(local.length + path.length + bytes.length, 16);
  return Buffer.concat([local, path, bytes, central, path, end]);
}

test('the downloadable deflated fonts.zip extracts to the exact repository fonts', async () => {
  const { context } = portal();
  const archive = readFileSync(new URL('../../web-flasher/public/fonts.zip', import.meta.url));
  const fonts = await context.unzipFonts(new File([archive], 'fonts.zip'));
  const manifest = JSON.parse(readFileSync(new URL('../../web-flasher/public/fonts/fonts.json', import.meta.url)));
  assert.equal(fonts.length, manifest.reduce((n, family) => n + family.files.length, 0));
  for (const font of fonts) {
    const family = manifest.find(f => f.files.includes(font.name));
    assert.ok(family, font.name);
    const original = readFileSync(new URL(`../../web-flasher/public/fonts/${family.dir}/${font.name}`, import.meta.url));
    assert.deepEqual(Buffer.from(await font.arrayBuffer()), original);
  }
});

test('stored ZIP entries work without browser decompression support', async () => {
  const { context } = portal();
  context.DecompressionStream = undefined;
  const fonts = await context.unzipFonts(new File([storedZip()], 'fonts.zip'));
  assert.equal(fonts[0].name, 'Test-Regular.ttf');
  assert.equal(await fonts[0].text(), '123456789');
});

test('invalid CRC, truncated ZIP, unsafe paths and empty font archives are rejected', async () => {
  const { context } = portal();
  const corrupt = storedZip();
  corrupt[30 + Buffer.byteLength('fonts/Test/Test-Regular.ttf')] ^= 1;
  for (const bytes of [corrupt, storedZip().subarray(0, 40), storedZip('../Test.ttf'), storedZip('README.txt')]) {
    await assert.rejects(context.unzipFonts(new File([bytes], 'fonts.zip')));
  }
});

test('duplicate output names across archive and loose files are rejected', async () => {
  const { context } = portal();
  await assert.rejects(context.expandFontFiles([
    new File([storedZip()], 'fonts.zip'), new File(['font'], 'test-regular.TTF'),
  ]));
});

test('choosing a ZIP uploads extracted fonts, never the archive itself', async () => {
  const { context, element, requests } = portal();
  element('font-input').files = [new File([storedZip()], 'fonts.zip')];
  await context.uploadFonts();
  assert.equal(requests.length, 1);
  assert.match(requests[0].url, /kind=font/);
  assert.equal(requests[0].data.args[2], 'Test-Regular.ttf');
});

test('bad archive sends no files and restores controls', async () => {
  const { context, element, requests } = portal();
  element('font-input').files = [new File(['broken'], 'fonts.zip')];
  await context.uploadFonts();
  assert.equal(requests.length, 0);
  assert.equal(element('font-input').disabled, false);
  assert.equal(element('btn-save').disabled, false);
  assert.equal(element('toast').className, 'toast err');
});

test('Flash selector disables families that exceed partition capacity', async () => {
  const { context, element } = portal([], { families: [{ name: 'Too large', bytes: 16384, fits: false }] });
  await context.loadFonts();
  assert.equal(element('font-flash-family').options[1].disabled, true);
  assert.match(element('font-flash-capacity').textContent, /8 KB/);
});

test('Flash installation sends the selected family and restores controls on failure', async () => {
  const { context, element, fetchCalls } = portal([], { failInstall: true });
  element('font-flash-family').value = 'Too large';
  await context.installPortalFont();
  const call = fetchCalls.find(c => c.url === '/api/fonts/install');
  assert.equal(call.options.method, 'POST');
  assert.equal(call.options.body, 'name=Too%20large');
  assert.match(element('toast').textContent, /Does not fit/);
  assert.equal(element('btn-save').disabled, false);
  assert.equal(element('btn-close').disabled, false);
});

test('installed family remains selectable for the remove action', async () => {
  const { context, element } = portal([], { families: [{ name: 'Literata', bytes: 1000, fits: true, installed: true }] });
  await context.loadFonts();
  const option = element('font-flash-family').options[1];
  assert.equal(option.disabled, false);
  assert.match(option.text, /JS_FONT_ALREADY/);
});

test('changed family with the same name remains available for installation', async () => {
  const { context, element } = portal([], { families: [{ name: 'Noto Sans', bytes: 1000, fits: true, installed: false }] });
  await context.loadFonts();
  assert.equal(element('font-flash-family').options[1].disabled, false);
});

test('upload of a family already in Flash is rejected before sending any file', async () => {
  const { context, element, requests } = portal();
  element('font-input').files = [new File(['data'], 'NotoSans-Regular.ttf')];
  await context.uploadFonts();
  assert.equal(requests.length, 0);
  assert.match(element('toast').textContent, /JS_FONT_DELETE_FIRST/);
});

test('built-in Noto Serif is skipped and never sent to the card', async () => {
  const { context, element, requests } = portal();
  element('font-input').files = [new File(['data'], 'NotoSerif-Regular.ttf')];
  await context.uploadFonts();
  assert.equal(requests.length, 0);
  assert.match(element('toast').textContent, /JS_FONT_BUILTIN_ONLY/);
});

test('removing the additional Flash font calls the delete endpoint and restores controls', async () => {
  const { context, element, fetchCalls } = portal();
  await context.loadFonts();
  element('font-flash-family').value = 'Noto Sans';
  await context.deletePortalFont();
  assert.ok(fetchCalls.some(call => call.url === '/api/fonts/flash/delete' && call.options.method === 'POST'));
  assert.equal(element('btn-save').disabled, false);
  assert.equal(element('btn-close').disabled, false);
  assert.match(element('toast').textContent, /JS_FONT_DELETED/);
});

test('after Flash removal the family may be uploaded to MicroSD', async () => {
  const { context, element, requests } = portal([], { installed: '' });
  element('font-input').files = [new File(['data'], 'NotoSans-Regular.ttf')];
  await context.uploadFonts();
  assert.equal(requests.length, 1);
});

test('MicroSD upload checks all installed Flash families, not just the first',async()=>{
  const { context, element, requests }=portal([], {installedFamilies:[{name:'Alpha',bytes:1000},{name:'Beta',bytes:1000}]});
  element('font-input').files=[new File(['font'],'Beta-Regular.ttf')];
  await context.uploadFonts();
  assert.equal(requests.length,0);
  assert.match(element('toast').textContent,/JS_FONT_DELETE_FIRST/);
});

test('one menu switches the action between installing and removing', async () => {
  const { context, element, fetchCalls } = portal();
  await context.loadFonts();
  element('font-flash-family').value = 'Noto Sans';
  context.updateFontAction();
  assert.match(element('font-flash-install').textContent, /JS_FLASH_DELETE/);
  assert.equal(element('font-flash-install').disabled, false);
  await context.runFontAction();
  assert.ok(fetchCalls.some(call => call.url === '/api/fonts/flash/delete' && call.options.body === 'name=Noto%20Sans'));
  element('font-flash-family').value = 'Literata';
  context.updateFontAction();
  assert.match(element('font-flash-install').textContent, /JS_FLASH_INSTALL/);
  await context.runFontAction();
  assert.ok(fetchCalls.some(call => call.url === '/api/fonts/install' && call.options.body === 'name=Literata'));
});

test('refresh preserves selected family and switches to remove after installation', async () => {
  const state={ installedFamilies: [] };
  const { context, element }=portal([],state);
  await context.loadFonts();
  element('font-flash-family').value='Literata';
  state.installedFamilies=[{name:'Literata',bytes:1000}];
  await context.loadFonts();
  assert.equal(element('font-flash-family').value,'Literata');
  assert.match(element('font-flash-install').textContent,/JS_FLASH_DELETE/);
});

test('built-in font is read-only in the unified menu and cannot dispatch either action',async()=>{
  const {context,element,fetchCalls}=portal();
  await context.loadFonts();
  const option=element('font-flash-family').options.find(o=>o.value==='Noto Serif');
  assert.equal(option.disabled,true);
  element('font-flash-family').value='Noto Serif';context.updateFontAction();
  assert.equal(element('font-flash-install').disabled,true);
  assert.match(element('font-flash-install').textContent,/JS_FLASH_BUILTIN/);
  const before=fetchCalls.length;await context.runFontAction();assert.equal(fetchCalls.length,before);
});
