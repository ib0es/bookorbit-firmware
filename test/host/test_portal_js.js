#!/usr/bin/env node
// The web portal's page (src/net/web_config.cpp) as a browser would run it.
//
//   node test/host/test_portal_js.js
//
// The page is filled in from the locales exactly as the device does, its script is
// compiled for every language, then run against a stand-in browser: a small DOM, a
// fetch / XMLHttpRequest that answer what the test says, and timers the test winds.
// Every function of the page is walked: books (list, delete, upload), fonts (list, SD
// and Flash delete, Flash install, upload, ZIP import), settings, closing.
//
// Why: one missing `try{` left the whole script unparsable on the device (the Upload
// button never woke up); a native confirm() answered "no" at once in the browser a
// phone opens for an access point, so no delete did anything. Nothing else looked at
// the page.

'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.resolve(__dirname, '..', '..');
const src = fs.readFileSync(path.join(ROOT, 'src/net/web_config.cpp'), 'utf8');
const raws = [...src.matchAll(/R"rawliteral\(([\s\S]*?)\)rawliteral"/g)].map((m) => m[1]);
const tokens = {};
for (const m of src.matchAll(/\{"(%[A-Z_]+%)",\s*MsgId::([A-Z_0-9]+),\s*(true|false)\}/g)) {
  tokens[m[1]] = { id: m[2], js: m[3] === 'true' };
}

let failed = 0;
let checks = 0;
function check(ok, what) {
  checks++;
  if (!ok) {
    failed++;
    console.log('FAIL  ' + what);
  }
}

// As jsEscape() in web_config.cpp: a string inside '...' in the script.
const jsEscape = (s) =>
  s.replace(/\\/g, '\\\\').replace(/'/g, "\\'").replace(/"/g, '\\"').replace(/\n/g, '\\n').replace(/</g, '\\x3c');

function readLocale(lang) {
  const file = path.join(ROOT, 'locales', lang + '.json');
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch (e) {
    check(false, `locales/${lang}.json is not valid JSON: ${e.message}`);
    return null;
  }
}

function renderPage(strings) {
  const html = raws.join('\n').replace(/%[A-Z_]+%/g, (t) => {
    const e = tokens[t];
    if (!e || strings[e.id] === undefined) return t;
    return e.js ? jsEscape(strings[e.id]) : strings[e.id];
  });
  const m = html.match(/<script>([\s\S]*?)<\/script>/);
  return { html, script: m ? m[1] : '', strings };
}

// ---------------------------------------------------------------- a stand-in browser
class El {
  constructor(id, className = '') {
    this.id = id;
    this.className = className;
    this.style = {};
    const classes = new Set(className.split(/\s+/).filter(Boolean));
    this.classList = {
      add: (c) => {
        classes.add(c);
        this.className = [...classes].join(' ');
      },
      remove: (c) => {
        classes.delete(c);
        this.className = [...classes].join(' ');
      },
      contains: (c) => classes.has(c),
    };
    this.disabled = false;
    this.checked = false;
    this.value = '';
    this.files = [];
    this.textContent = '';
    this.innerHTML = '';
    this.children = [];
    this.options = [];
    this.handlers = {};
    this.fields = [];
    const named = {};
    this.elements = { namedItem: (n) => named[n] || (named[n] = new El(n)) };
  }
  addEventListener(type, fn) {
    (this.handlers[type] = this.handlers[type] || []).push(fn);
  }
  fire(type, ev = {}) {
    for (const fn of this.handlers[type] || []) fn.call(this, Object.assign({ preventDefault() {} }, ev));
  }
  click() {
    if (this.disabled) return;
    if (this.onclick) this.onclick();
  }
  appendChild(c) {
    this.children.push(c);
    return c;
  }
  add(opt) {
    this.options.push(opt);
  }
  remove(i) {
    if (typeof i === 'number') this.options.splice(i, 1);
  }
}

function browser(page, search) {
  const els = {};
  const calls = { fetch: [], xhr: [], native: [] };
  const timers = [];
  const routes = [];
  const world = { els, calls, routes, page };

  // The markup's own state: buttons born disabled, boxes born checked, classes.
  for (const m of page.html.matchAll(/<(?:div|button|input|select|form|label|progress|span)\b[^>]*>/g)) {
    const id = (m[0].match(/\bid='([^']+)'/) || [])[1];
    if (!id) continue;
    const el = (els[id] = new El(id, (m[0].match(/\bclass='([^']*)'/) || [])[1] || ''));
    if (/\bdisabled\b/.test(m[0].replace(/\b(id|class|title)='[^']*'/g, ''))) el.disabled = true;
    if (/\bchecked\b/.test(m[0])) el.checked = true;
    if (/\bdisplay:none\b/.test(m[0])) el.style.display = 'none';
  }

  // What the "server" answers: route(method, urlStart, status, body-or-function).
  world.route = (method, start, status, body) => {
    routes.unshift({ method, start, status, body });
  };
  world.reset = () => {
    calls.fetch.length = 0;
    calls.xhr.length = 0;
  };
  world.fetches = (start) => calls.fetch.filter((c) => c.url.startsWith(start));
  world.settle = async () => {
    for (let i = 0; i < 12; i++) await new Promise((r) => setImmediate(r));
  };
  world.timers = () => timers.filter((t) => !t.cleared);
  world.runTimers = () => {
    for (const t of timers.filter((x) => !x.cleared)) {
      t.cleared = true;
      t.fn();
    }
  };
  world.run = (code) => vm.runInContext(code, world.context);
  world.$ = (id) => els[id] || (els[id] = new El(id));

  class XHR {
    constructor() {
      this.upload = {};
      this.status = 0;
      this.responseText = '';
      calls.xhr.push(this);
    }
    open(method, url) {
      this.method = method;
      this.url = url;
    }
    send(body) {
      this.sent = body;
    }
    progress(loaded, total) {
      this.upload.onprogress({ lengthComputable: true, loaded, total });
    }
    complete(status, body) {
      this.status = status;
      this.responseText = typeof body === 'string' ? body : JSON.stringify(body);
      this.onload();
    }
    fail() {
      this.onerror();
    }
  }
  class FormDataStub {
    constructor(form) {
      this.parts = form && form.fields ? form.fields.map((f) => [f[0], f[1]]) : [];
    }
    append(k, v, name) {
      this.parts.push([k, name || v]);
    }
    [Symbol.iterator]() {
      return this.parts[Symbol.iterator]();
    }
  }
  const native = (what) => () => {
    calls.native.push(what);
    return false;   // a captive-portal browser answers "no" without showing anything
  };
  const context = {
    console,
    URLSearchParams,
    Blob,
    File,
    TextDecoder,
    DecompressionStream,
    setTimeout: (fn, ms) => {
      timers.push({ fn, ms, cleared: false });
      return timers.length;
    },
    clearTimeout: (id) => {
      if (timers[id - 1]) timers[id - 1].cleared = true;
    },
    XMLHttpRequest: XHR,
    FormData: FormDataStub,
    Option: function (text, value) {
      this.text = text;
      this.value = value;
      this.disabled = false;
    },
    window: { location: { search } },
    document: {
      getElementById: (id) => els[id] || (els[id] = new El(id)),
      querySelectorAll: (sel) => Object.values(els).filter((e) => e.className.split(/\s+/).includes(sel.slice(1))),
      // An element made by the page is found by its id once it is given one.
      createElement: () => {
        const e = new El('new');
        let id = 'new';
        Object.defineProperty(e, 'id', {
          get: () => id,
          set: (v) => {
            id = v;
            els[v] = e;
          },
        });
        return e;
      },
    },
    fetch: (url, init = {}) => {
      const call = { url, method: init.method || 'GET', body: init.body };
      calls.fetch.push(call);
      const route = routes.find((r) => r.method === call.method && url.startsWith(r.start));
      if (!route) return new Promise(() => {});   // no answer: only the request is looked at
      if (route.status === 'down') return Promise.reject(new Error('network down'));
      const body = typeof route.body === 'function' ? route.body(call) : route.body;
      return Promise.resolve({ ok: route.status < 400, status: route.status, json: async () => JSON.parse(JSON.stringify(body)) });
    },
    confirm: native('confirm'),
    alert: native('alert'),
    prompt: native('prompt'),
  };
  world.context = vm.createContext(context);
  return world;
}

// A stored ZIP (what unzipFonts() reads): [{ name, bytes }].
function makeZip(entries) {
  const crcTable = [];
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    crcTable[n] = c >>> 0;
  }
  const crc32 = (b) => {
    let c = 0xffffffff;
    for (const x of b) c = crcTable[(c ^ x) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  };
  const chunks = [];
  const central = [];
  let offset = 0;
  for (const e of entries) {
    const name = Buffer.from(e.name);
    const bytes = Buffer.from(e.bytes);
    const local = Buffer.alloc(30);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(20, 4);
    local.writeUInt32LE(crc32(bytes), 14);
    local.writeUInt32LE(bytes.length, 18);
    local.writeUInt32LE(bytes.length, 22);
    local.writeUInt16LE(name.length, 26);
    chunks.push(local, name, bytes);
    const cd = Buffer.alloc(46);
    cd.writeUInt32LE(0x02014b50, 0);
    cd.writeUInt16LE(20, 4);
    cd.writeUInt16LE(20, 6);
    cd.writeUInt32LE(crc32(bytes), 16);
    cd.writeUInt32LE(bytes.length, 20);
    cd.writeUInt32LE(bytes.length, 24);
    cd.writeUInt16LE(name.length, 28);
    cd.writeUInt32LE(offset, 42);
    central.push(cd, name);
    offset += 30 + name.length + bytes.length;
  }
  const dir = Buffer.concat(central);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(entries.length, 8);
  end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(dir.length, 12);
  end.writeUInt32LE(offset, 16);
  const all = Buffer.concat([...chunks, dir, end]);
  return {
    name: 'fonts.zip',
    size: all.length,
    arrayBuffer: async () => all.buffer.slice(all.byteOffset, all.byteOffset + all.length),
  };
}

const ttf = (name, size = 64) => ({ name, size, arrayBuffer: async () => new ArrayBuffer(size) });

// ---------------------------------------------------------------- every language compiles
const languages = fs
  .readdirSync(path.join(ROOT, 'locales'))
  .filter((f) => f.endsWith('.json'))
  .map((f) => f.replace(/\.json$/, ''));
const pages = {};
for (const lang of languages) {
  const strings = readLocale(lang);
  if (!strings) continue;
  const page = renderPage(strings);
  check(page.script.length > 0, `${lang}: the page has a script`);
  try {
    new vm.Script(page.script, { filename: `portal_${lang}.js` });
    check(true, '');
  } catch (e) {
    check(false, `${lang}: the portal script does not compile: ${e.message}`);
    continue;
  }
  pages[lang] = page;
}

// ---------------------------------------------------------------- the scenarios
const BOOKS = { free: 1048576, total: 4194304, books: [{ name: 'A.epub', size: 2048 }, { name: 'B.fb2', size: 1024 }] };
const FONTS = {
  builtin: 'Noto Serif',
  capacity: 3 << 20,
  free: 2 << 20,
  used: 1 << 20,
  installedFamilies: [{ name: 'Literata', bytes: 100 }],
  families: [
    { name: 'Literata', bytes: 100, installed: true, fits: true },
    { name: 'Lora', bytes: 200, installed: false, fits: true },
    { name: 'Huge', bytes: 9000000, installed: false, fits: false },
  ],
};

async function start(lang, search = '?pin=123456', setup) {
  const w = browser(pages[lang], search);
  w.route('GET', '/api/books', 200, BOOKS);
  w.route('GET', '/api/fonts', 200, FONTS);
  if (setup) setup(w);
  vm.runInContext(pages[lang].script, w.context);
  await w.settle();
  return w;
}
const toastOf = (w) => w.$('toast');

async function scenarios(lang) {
  const P = (what) => `${lang}: ${what}`;
  const T = (id) => pages[lang].strings[id];
  let w;

  // ---- start: the book list is asked for, the controls are as the markup says
  w = await start(lang);
  check(w.fetches('/api/books').length === 1, P('the page asks for the book list at start'));
  check(w.$('btn-up').disabled === true, P('Upload is off before a file is chosen'));
  check(w.$('font-up').disabled === true, P('the font Upload is off before a file is chosen'));

  // ---- books: list
  check(w.$('blist').children.length === 2, P('the book list shows every book'));
  check(w.$('book-cnt').textContent === 2, P('the book count is shown'));
  check(/MB|KB/.test(w.$('sd-info').textContent), P('the free / total space of the card is shown'));
  const names = w.$('blist').children.map((r) => r.children[0].textContent);
  check(names[0].startsWith('A.epub') && names[1].startsWith('B.fb2'), P(`the books are named: ${names}`));
  w = await start(lang, '?pin=123456', (b) => b.route('GET', '/api/books', 200, { free: 0, total: 0, books: [] }));
  check(w.$('blist').innerHTML.includes(T('WEB_HTML_NO_BOOKS')), P('an empty list says so'));

  // ---- books: delete (two presses on the button, no native dialog)
  w = await start(lang);
  const del = () => w.$('blist').children[0].children[1];
  check(del().textContent === T('WEB_HTML_BTN_DELETE'), P('the delete button says Delete'));
  w.reset();
  del().click();
  check(w.calls.fetch.length === 0, P('the first press deletes nothing'));
  check(del().textContent === T('WEB_HTML_BTN_DELETE') + '?', P('the first press asks on the button itself'));
  w.route('POST', '/api/books/delete', 200, { ok: true });
  del().click();
  await w.settle();
  const posts = w.fetches('/api/books/delete');
  check(posts.length === 1 && posts[0].url === '/api/books/delete?pin=123456', P('the second press deletes, with the PIN'));
  check(posts[0] && /^name=A\.epub$/.test(posts[0].body), P('the book named is the one pressed'));
  check(w.fetches('/api/books').length >= 1, P('the list is read again after a delete'));
  // a refusal is shown
  w = await start(lang);
  w.route('POST', '/api/books/delete', 404, { ok: false, error: 'File not found' });
  del().click();
  del().click();
  await w.settle();
  check(toastOf(w).textContent === 'File not found' && /err/.test(toastOf(w).className), P('a refused delete shows the reason'));
  // the question lapses
  w = await start(lang);
  del().click();
  w.runTimers();
  check(del().textContent === T('WEB_HTML_BTN_DELETE'), P('an unanswered question lapses'));
  w.reset();
  del().click();
  check(w.calls.fetch.length === 0, P('a press after the lapse asks again'));

  // ---- books: upload
  w = await start(lang);
  w.$('file-input').files = [
    { name: 'One.epub', size: 1000 },
    { name: 'Two.epub', size: 3000 },
    { name: 'Three.epub', size: 500 },
  ];
  w.$('file-input').fire('change');
  check(w.$('btn-up').disabled === false, P('Upload is on once books are chosen'));
  check(w.$('queue').children.length === 3, P('the chosen books are queued'));
  w.run('startUpload()');
  const xhr = w.calls.xhr[0];
  check(xhr.method === 'POST' && xhr.url === '/api/upload?pin=123456&overwrite=0&keep_progress=1&sizes=1000,3000,500', P(`Upload goes out with the PIN and the sizes: ${xhr.url}`));
  check(xhr.sent.parts.length === 3, P('every chosen book is in the one request'));
  check(w.$('btn-up').disabled && w.$('file-input').disabled, P('the controls are off while uploading'));
  xhr.progress(2250, 4500);
  check(w.$('p-batch').value === 50, P('the progress follows the request'));
  xhr.complete(200, { files: [{ ok: true }, { exists: true }, { error: 'No space' }] });
  const badges = w.$('queue').children.map((r) => r.children[1].textContent);
  check(badges.join(',') === 'OK,skip,No space', P(`each book gets its own result: ${badges}`));
  check(w.$('file-input').disabled === false && w.$('btn-up').disabled === true, P('the controls come back, Upload waits for new books'));
  check(w.timers().length >= 1, P('the book list is read again after an upload'));
  // flags
  w = await start(lang);
  w.$('overwrite').checked = true;
  w.$('overwrite').fire('change');
  check(w.$('lbl-keep-prog').style.display === 'flex', P('overwriting offers to keep the reading progress'));
  w.$('keep-progress').checked = false;
  w.$('file-input').files = [{ name: 'One.epub', size: 10 }];
  w.$('file-input').fire('change');
  w.run('startUpload()');
  check(w.calls.xhr[0].url.includes('&overwrite=1&keep_progress=0&'), P('the overwrite and keep-progress boxes go out'));
  w.calls.xhr[0].fail();
  check(w.$('queue').children[0].children[1].textContent === 'err', P('a failed upload marks the book'));
  check(w.$('file-input').disabled === false, P('a failed upload frees the controls'));
  // no second upload while one runs
  w = await start(lang);
  w.$('file-input').files = [{ name: 'One.epub', size: 10 }];
  w.$('file-input').fire('change');
  w.run('startUpload()');
  w.run('startUpload()');
  check(w.calls.xhr.length === 1, P('a second upload does not start while one runs'));

  // ---- tabs
  w = await start(lang);
  w.reset();
  w.run("showTab('fonts', document.getElementById('tab-btn-fonts'))");
  await w.settle();
  check(w.$('tab-fonts').classList.contains('active'), P('the Fonts tab opens'));
  check(w.fetches('/api/fonts').length === 1, P('opening the Fonts tab reads the fonts'));

  // ---- fonts: lists
  w = await start(lang);
  w.run('loadFonts()');
  await w.settle();
  check(w.$('font-list').children.length === 3, P('every font on the card is listed'));
  const sdButtons = w.$('font-list').children.map((r) => r.children[0].textContent);
  check(sdButtons.every((t) => t === T('WEB_HTML_FONT_SD_DELETE')), P('each font on the card has its delete button'));
  const flash = w.$('font-flash-family').options;
  const opt = (name) => flash.find((o) => o.value === name);
  check(opt('Huge') && opt('Huge').disabled, P('a font that does not fit cannot be picked for Flash'));
  check(opt('Noto Serif') && opt('Noto Serif').disabled, P('the built-in font cannot be picked for Flash'));
  check(opt('Lora') && !opt('Lora').disabled && opt('Literata') && !opt('Literata').disabled, P('a font that fits, and an installed one, can be picked'));
  const family = w.$('cfg-form').elements.namedItem('font_family').options.map((o) => o.value);
  check(['Literata', 'Lora', 'Huge'].every((n) => family.includes(n)), P(`the reading-font list has the card's fonts: ${family}`));
  check(/Flash:/.test(w.$('font-flash-capacity').textContent), P('the Flash space is shown'));
  // the button follows the pick
  const flashBtn = () => w.$('font-flash-install');
  w.$('font-flash-family').value = 'Lora';
  w.run('updateFontAction()');
  check(flashBtn().textContent === T('WEB_HTML_FONT_FLASH_INSTALL') && !flashBtn().disabled, P('a font not in Flash offers to install'));
  w.$('font-flash-family').value = 'Literata';
  w.run('updateFontAction()');
  check(flashBtn().textContent === T('WEB_HTML_FONT_FLASH_DELETE') && !flashBtn().disabled, P('a font in Flash offers to delete'));
  w.$('font-flash-family').value = 'Huge';
  w.run('updateFontAction()');
  check(flashBtn().disabled, P('a font that does not fit offers nothing'));
  w.$('font-flash-family').value = 'Noto Serif';
  w.run('updateFontAction()');
  check(flashBtn().disabled, P('the built-in font offers nothing'));

  // ---- fonts: install in Flash
  w = await start(lang);
  w.run('loadFonts()');
  await w.settle();
  w.$('font-flash-family').value = 'Lora';
  w.route('POST', '/api/fonts/install', 200, { ok: true });
  w.reset();
  w.run('runFontAction()');
  await w.settle();
  const inst = w.fetches('/api/fonts/install');
  check(inst.length === 1 && inst[0].url === '/api/fonts/install?pin=123456' && inst[0].body === 'name=Lora', P('Install goes out with the PIN and the name'));
  check(w.$('cfg-form').elements.namedItem('font_family').value === 'Lora', P('an installed font becomes the reading font'));
  check(toastOf(w).textContent === T('WEB_HTML_FONT_FLASH_DONE') && /ok/.test(toastOf(w).className), P('an install says it is done'));
  check(w.$('btn-save').disabled === false && w.$('btn-close').disabled === false, P('Save and Close come back after an install'));
  check(w.fetches('/api/fonts').length >= 1, P('the fonts are read again after an install'));
  w = await start(lang);
  w.run('loadFonts()');
  await w.settle();
  w.$('font-flash-family').value = 'Lora';
  w.route('POST', '/api/fonts/install', 500, { ok: false, error: 'Flash is full' });
  w.run('runFontAction()');
  await w.settle();
  check(toastOf(w).textContent === 'Flash is full' && /err/.test(toastOf(w).className), P('a refused install shows the reason'));
  check(w.$('btn-save').disabled === false, P('Save comes back after a refused install'));

  // ---- fonts: delete from Flash (two presses, no native dialog)
  w = await start(lang);
  w.run('loadFonts()');
  await w.settle();
  w.$('cfg-form').elements.namedItem('font_family').value = 'Literata';
  w.$('font-flash-family').value = 'Literata';
  w.run('updateFontAction()');
  w.route('POST', '/api/fonts/flash/delete', 200, { ok: true });
  w.reset();
  w.run('runFontAction()');
  check(w.calls.fetch.length === 0, P('the first press on a Flash font deletes nothing'));
  check(flashBtn().textContent === T('WEB_HTML_BTN_DELETE') + '?', P('the first press asks on the button'));
  w.run('runFontAction()');
  await w.settle();
  const fd = w.fetches('/api/fonts/flash/delete');
  check(fd.length === 1 && fd[0].url === '/api/fonts/flash/delete?pin=123456' && fd[0].body === 'name=Literata', P('the second press deletes it, with the PIN'));
  check(w.$('cfg-form').elements.namedItem('font_family').value === '', P('deleting the reading font clears the choice'));
  check(toastOf(w).textContent === T('WEB_HTML_FONT_DELETED'), P('a Flash delete says it is done'));
  w = await start(lang);
  w.run('loadFonts()');
  await w.settle();
  w.$('font-flash-family').value = 'Literata';
  w.run('updateFontAction()');
  w.run('runFontAction()');
  w.runTimers();
  w.reset();
  w.run('runFontAction()');
  check(w.calls.fetch.length === 0, P('an unanswered Flash question lapses'));

  // ---- fonts: delete from the card
  w = await start(lang);
  w.run('loadFonts()');
  await w.settle();
  w.$('cfg-form').elements.namedItem('font_family').value = 'Lora';
  w.route('POST', '/api/fonts/card/delete', 200, { ok: true, inFlash: false });
  const sdRemove = (i) => w.$('font-list').children[i].children[0];
  w.reset();
  sdRemove(1).click();
  check(w.calls.fetch.length === 0 && sdRemove(1).textContent === T('WEB_HTML_BTN_DELETE') + '?', P('the first press on a card font asks'));
  sdRemove(1).click();
  await w.settle();
  const cd = w.fetches('/api/fonts/card/delete');
  check(cd.length === 1 && cd[0].url === '/api/fonts/card/delete?pin=123456' && cd[0].body === 'name=Lora', P('the second press deletes the card font, with the PIN'));
  check(w.$('cfg-form').elements.namedItem('font_family').value === '', P('deleting the reading font from the card clears the choice'));
  check(toastOf(w).textContent === T('WEB_HTML_FONT_SD_DELETED'), P('a card delete says it is done'));
  // a copy in Flash keeps the choice
  w = await start(lang);
  w.run('loadFonts()');
  await w.settle();
  w.$('cfg-form').elements.namedItem('font_family').value = 'Literata';
  w.route('POST', '/api/fonts/card/delete', 200, { ok: true, inFlash: true });
  sdRemove(0).click();
  sdRemove(0).click();
  await w.settle();
  check(w.$('cfg-form').elements.namedItem('font_family').value === 'Literata', P('a copy in Flash keeps the reading font'));
  w = await start(lang);
  w.run('loadFonts()');
  await w.settle();
  w.route('POST', '/api/fonts/card/delete', 409, { ok: false, error: 'Built-in font cannot be modified' });
  sdRemove(0).click();
  sdRemove(0).click();
  await w.settle();
  check(toastOf(w).textContent === 'Built-in font cannot be modified', P('a refused card delete shows the reason'));
  check(w.$('btn-save').disabled === false, P('Save comes back after a refused delete'));

  // ---- fonts: upload (TTF)
  w = await start(lang);
  w.$('font-input').files = [ttf('Alegreya-Regular.ttf', 700)];
  w.$('font-input').fire('change');
  check(w.$('font-up').disabled === false, P('font Upload is on once a font is chosen'));
  w.run('uploadFonts()');
  await w.settle();
  const fx = w.calls.xhr[0];
  check(fx && fx.url === '/api/upload?kind=font&pin=123456&overwrite=0&sizes=700', P(`a font goes out with the PIN: ${fx && fx.url}`));
  fx.progress(50, 100);
  check(w.$('font-progress').value === 50, P('the font progress follows the request'));
  fx.complete(200, { files: [{ ok: true }] });
  await w.settle();
  check(/Alegreya-Regular\.ttf: OK/.test(w.$('font-result').textContent), P('a stored font is reported'));
  check(toastOf(w).textContent === T('WEB_HTML_FONTS_DONE'), P('the upload says it is done'));
  check(w.$('font-input').disabled === false && w.$('btn-save').disabled === false, P('the controls come back after a font upload'));
  // an existing one, without overwrite
  w = await start(lang);
  w.$('font-input').files = [ttf('Alegreya-Regular.ttf')];
  w.run('uploadFonts()');
  await w.settle();
  w.calls.xhr[0].complete(200, { files: [{ exists: true }] });
  await w.settle();
  check(w.$('font-result').textContent.includes(T('WEB_HTML_FONT_EXISTS')), P('a font that exists is said to'));
  // overwrite goes out
  w = await start(lang);
  w.$('font-overwrite').checked = true;
  w.$('font-input').files = [ttf('Alegreya-Regular.ttf')];
  w.run('uploadFonts()');
  await w.settle();
  check(w.calls.xhr[0].url.includes('&overwrite=1&'), P('the font overwrite box goes out'));
  // a font already in Flash must go first
  w = await start(lang);
  w.$('font-input').files = [ttf('Literata-Regular.ttf')];
  w.run('uploadFonts()');
  await w.settle();
  check(w.calls.xhr.length === 0 && toastOf(w).textContent.includes(T('WEB_HTML_FONT_DELETE_FIRST')), P('a font in Flash must be deleted first'));
  w = await start(lang);
  w.$('font-input').files = [ttf('NotoSerif-Regular.ttf')];
  w.run('uploadFonts()');
  await w.settle();
  check(w.calls.xhr.length === 0 && toastOf(w).textContent.includes(T('WEB_HTML_FONT_BUILTIN_ONLY')), P('the built-in font is not uploaded'));
  w = await start(lang);
  w.$('font-input').files = [ttf('Alegreya-Regular.ttf')];
  w.run('uploadFonts()');
  await w.settle();
  w.calls.xhr[0].fail();
  await w.settle();
  check(/err/.test(toastOf(w).className) && w.$('font-input').disabled === false, P('a failed font upload says so and frees the controls'));

  // ---- fonts: ZIP
  w = await start(lang);
  w.$('font-input').files = [makeZip([{ name: 'Alegreya/Alegreya-Regular.ttf', bytes: Buffer.alloc(300, 1) }, { name: 'Alegreya/readme.txt', bytes: 'hi' }])];
  w.run('uploadFonts()');
  await w.settle();
  check(w.calls.xhr.length === 1 && w.calls.xhr[0].url.endsWith('&sizes=300') && w.calls.xhr[0].sent.parts[0][1] === 'Alegreya-Regular.ttf', P('a ZIP is opened in the browser: its fonts go out one by one'));
  w = await start(lang);
  w.$('font-input').files = [makeZip([{ name: '../evil.ttf', bytes: Buffer.alloc(30, 1) }])];
  w.run('uploadFonts()');
  await w.settle();
  check(w.calls.xhr.length === 0 && toastOf(w).textContent.includes(T('WEB_HTML_ZIP_ERROR')), P('a ZIP with a path out of its folder is refused'));
  w = await start(lang);
  w.$('font-input').files = [makeZip([{ name: 'readme.txt', bytes: 'hi' }])];
  w.run('uploadFonts()');
  await w.settle();
  check(w.calls.xhr.length === 0 && toastOf(w).textContent.includes(T('WEB_HTML_ZIP_EMPTY')), P('a ZIP without fonts says so'));
  w = await start(lang);
  w.$('font-input').files = [ttf('notes.txt')];
  w.run('uploadFonts()');
  await w.settle();
  check(w.calls.xhr.length === 0 && /err/.test(toastOf(w).className), P('a file that is not a font is refused'));

  // ---- settings
  w = await start(lang);
  w.$('cfg-form').fields = [['wifi_ssid', 'Home'], ['font_family', 'Lora'], ['sync_on_boot', '1']];
  w.route('POST', '/save', 200, { ok: true });
  w.reset();
  w.$('cfg-form').fire('submit');
  await w.settle();
  const sv = w.fetches('/save');
  check(sv.length === 1 && sv[0].url === '/save?ajax=1&pin=123456' && sv[0].body === 'wifi_ssid=Home&font_family=Lora&sync_on_boot=1', P(`Save sends the form with the PIN: ${sv[0] && sv[0].body}`));
  check(toastOf(w).textContent === T('WEB_HTML_STATUS_SAVED') && /ok/.test(toastOf(w).className), P('Save says it is done'));
  w.route('POST', '/save', 401, { ok: false, error: 'PIN required' });
  w.$('cfg-form').fire('submit');
  await w.settle();
  check(toastOf(w).textContent === 'PIN required' && /err/.test(toastOf(w).className), P('a refused Save shows the reason'));
  w.route('POST', '/save', 'down');
  w.$('cfg-form').fire('submit');
  await w.settle();
  check(toastOf(w).textContent === T('WEB_HTML_STATUS_ERROR'), P('a Save with no network says so'));

  // ---- closing
  w = await start(lang);
  w.route('POST', '/api/close', 200, { ok: true });
  w.reset();
  w.run('closePortal()');
  await w.settle();
  check(w.calls.fetch.length === 1 && w.calls.fetch[0].url === '/api/close?pin=123456', P('Close goes out with the PIN'));
  check(toastOf(w).textContent === T('WEB_HTML_PORTAL_CLOSED'), P('Close says the portal is closed'));

  // ---- the access point has no PIN: the requests go out all the same
  w = await start(lang, '');
  w.route('POST', '/api/close', 200, { ok: true });
  w.run('closePortal()');
  check(w.calls.fetch[w.calls.fetch.length - 1].url === '/api/close?pin=', P('without a PIN the requests still go out (access point mode)'));

  // ---- never a native dialog: a captive-portal browser would answer "no" at once
  const dialogs = [];
  for (const name of ['delBook', 'deletePortalCardFont', 'deletePortalFont', 'startUpload', 'uploadFonts']) {
    check(!new RegExp('\\b(confirm|alert|prompt)\\(').test(extractFunction(pages[lang].script, name)), P(`${name}() asks no native dialog`));
  }
  void dialogs;
}

// The text of one top-level function of the script.
function extractFunction(script, name) {
  const at = script.search(new RegExp('(async\\s+)?function\\s+' + name + '\\s*\\('));
  if (at < 0) return '';
  const open = script.indexOf('{', at);
  let depth = 0;
  for (let i = open; i < script.length; i++) {
    if (script[i] === '{') depth++;
    if (script[i] === '}' && --depth === 0) return script.slice(at, i + 1);
  }
  return '';
}

(async () => {
  for (const lang of ['en', 'ru']) {
    if (!pages[lang]) continue;
    try {
      await scenarios(lang);
    } catch (e) {
      check(false, `${lang}: the walk through the page stopped: ${e.stack}`);
    }
  }
  const natives = 0;
  void natives;
  console.log(`${failed ? 'FAILED' : 'OK'}: ${checks - failed}/${checks} portal page checks passed`);
  process.exit(failed ? 1 : 0);
})();
