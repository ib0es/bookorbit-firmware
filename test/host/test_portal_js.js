#!/usr/bin/env node
// The web portal's page (src/net/web_config.cpp) as a browser would run it:
// the template is filled in from the locales exactly as the device does, the script is
// compiled for every language, then run against a small stand-in for the DOM.
//
//   node test/host/test_portal_js.js
//
// Why: one missing `try{` left the whole script unparsable on the device, and the
// Upload button never woke up. Nothing else in the tests looks at the page.

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
  let data;
  try {
    data = JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch (e) {
    check(false, `locales/${lang}.json is not valid JSON: ${e.message}`);
    return null;
  }
  return data;
}

function pageScript(strings) {
  const html = raws.join('\n').replace(/%[A-Z_]+%/g, (t) => {
    const e = tokens[t];
    if (!e || strings[e.id] === undefined) return t;
    return e.js ? jsEscape(strings[e.id]) : strings[e.id];
  });
  const m = html.match(/<script>([\s\S]*?)<\/script>/);
  return { html, script: m ? m[1] : '' };
}

// ---------------------------------------------------------------- a stand-in browser
class El {
  constructor(id) {
    this.id = id;
    this.style = {};
    this.classList = { add() {}, remove() {} };
    this.disabled = false;
    this.checked = false;
    this.value = '';
    this.files = [];
    this.textContent = '';
    this.innerHTML = '';
    this.children = [];
    this.handlers = {};
    this.options = [];
    this.elements = { namedItem: () => new El('field') };
  }
  addEventListener(type, fn) {
    (this.handlers[type] = this.handlers[type] || []).push(fn);
  }
  fire(type) {
    for (const fn of this.handlers[type] || []) fn.call(this, {});
  }
  appendChild(c) {
    this.children.push(c);
    return c;
  }
  add() {}
  remove() {}
}

function browser(search, html) {
  const els = {};
  // The markup's own state: buttons born disabled, boxes born checked.
  for (const m of html.matchAll(/<(?:button|input)\b[^>]*>/g)) {
    const id = (m[0].match(/\bid='([^']+)'/) || [])[1];
    if (!id) continue;
    const el = (els[id] = new El(id));
    if (/\bdisabled\b/.test(m[0].replace(/\bid='[^']*'/, ''))) el.disabled = true;
    if (/\bchecked\b/.test(m[0])) el.checked = true;
  }
  const calls = { fetch: [], xhr: [], confirms: [] };
  const world = {
    answer: true,
    calls,
    els,
  };
  class XHR {
    constructor() {
      this.upload = {};
      calls.xhr.push(this);
    }
    open(method, url, async) {
      this.method = method;
      this.url = url;
    }
    send(body) {
      this.sent = body;
    }
  }
  class FormDataStub {
    constructor() {
      this.parts = [];
    }
    append(k, v, name) {
      this.parts.push([k, name || v]);
    }
  }
  const context = {
    console,
    URLSearchParams,
    encodeURIComponent,
    Math,
    JSON,
    Object,
    Array,
    Promise,
    setTimeout: () => 0,
    XMLHttpRequest: XHR,
    FormData: FormDataStub,
    window: { location: { search } },
    document: {
      getElementById: (id) => els[id] || (els[id] = new El(id)),
      querySelectorAll: () => [],
      createElement: () => new El('new'),
    },
    fetch: (url, init) => {
      calls.fetch.push({ url, init });
      return new Promise(() => {});   // never answers: only the request is looked at
    },
    confirm: (msg) => {
      calls.confirms.push(msg);
      return world.answer;
    },
  };
  world.context = vm.createContext(context);
  return world;
}

// ---------------------------------------------------------------- every language compiles
const languages = fs
  .readdirSync(path.join(ROOT, 'locales'))
  .filter((f) => f.endsWith('.json'))
  .map((f) => f.replace(/\.json$/, ''));
const pages = {};
for (const lang of languages) {
  const strings = readLocale(lang);
  if (!strings) continue;
  const page = pageScript(strings);
  const left = [...page.html.matchAll(/%[A-Z_]+%/g)].map((m) => m[0]).filter((t) => tokens[t] === undefined && t !== '%DEVICE%');
  check(page.script.length > 0, `${lang}: the page has a script`);
  try {
    new vm.Script(page.script, { filename: `portal_${lang}.js` });
    check(true, '');
  } catch (e) {
    check(false, `${lang}: the portal script does not compile: ${e.message}`);
    continue;
  }
  pages[lang] = page;
  void left;
}

// ---------------------------------------------------------------- what the page does
for (const lang of ['en', 'ru']) {
  if (!pages[lang]) continue;
  const w = browser('?pin=123456', pages[lang].html);
  try {
    vm.runInContext(pages[lang].script, w.context);
  } catch (e) {
    check(false, `${lang}: the script throws at start: ${e.message}`);
    continue;
  }
  const { els, calls } = w;
  const $ = (id) => els[id] || (els[id] = new El(id));
  const run = (code) => vm.runInContext(code, w.context);

  // The reported bug: a book chosen, Upload must wake up.
  check($('btn-up').disabled === true, `${lang}: Upload is off before a file is chosen`);
  $('file-input').files = [{ name: 'Book.epub', size: 1000 }];
  $('file-input').fire('change');
  check($('btn-up').disabled === false, `${lang}: Upload is on once a book is chosen`);

  // Upload: one request, with the PIN of the page and the flags.
  run('startUpload()');
  const xhr = calls.xhr[0];
  check(xhr && xhr.method === 'POST', `${lang}: Upload sends a POST`);
  check(
    xhr && xhr.url.startsWith('/api/upload?pin=123456&overwrite=0&keep_progress=1&sizes=1000'),
    `${lang}: Upload carries the PIN and the options: ${xhr && xhr.url}`,
  );
  check(xhr && xhr.sent && xhr.sent.parts.length === 1, `${lang}: the file is in the request`);

  run('uploading=false');   // the stand-in request never completes

  // Overwrite shows the "keep progress" choice.
  $('overwrite').checked = true;
  $('overwrite').fire('change');
  check($('lbl-keep-prog').style.display === 'flex', `${lang}: overwriting offers to keep the reading progress`);

  // Deleting asks first, and goes out with the PIN.
  calls.fetch.length = 0;
  calls.confirms.length = 0;
  w.answer = false;
  run("delBook('Old.epub')");
  check(calls.confirms.length === 1 && calls.fetch.length === 0, `${lang}: a book is not deleted when the question is declined`);
  w.answer = true;
  run("delBook('Old.epub')");
  check(
    calls.fetch.length === 1 && calls.fetch[0].url === '/api/books/delete?pin=123456' && /name=Old\.epub/.test(calls.fetch[0].init.body),
    `${lang}: a book is deleted with the PIN once confirmed`,
  );

  // SD font: the same.
  calls.fetch.length = 0;
  calls.confirms.length = 0;
  w.answer = false;
  run("deletePortalCardFont('Literata')");
  check(calls.confirms.length === 1 && calls.fetch.length === 0, `${lang}: an SD font is not deleted when declined`);
  w.answer = true;
  run("deletePortalCardFont('Literata')");
  check(calls.fetch.length === 1 && calls.fetch[0].url === '/api/fonts/card/delete?pin=123456', `${lang}: an SD font is deleted with the PIN once confirmed`);

  // Flash font: the same.
  run('fontUploading=false');
  calls.fetch.length = 0;
  calls.confirms.length = 0;
  $('font-flash-family').value = 'Literata';
  w.answer = false;
  run('deletePortalFont()');
  check(calls.confirms.length === 1 && calls.fetch.length === 0, `${lang}: a Flash font is not deleted when declined`);
  w.answer = true;
  run('deletePortalFont()');
  check(calls.fetch.length === 1 && calls.fetch[0].url === '/api/fonts/flash/delete?pin=123456', `${lang}: a Flash font is deleted with the PIN once confirmed`);

  // Closing and saving carry the PIN too.
  calls.fetch.length = 0;
  run('closePortal()');
  check(calls.fetch.length === 1 && calls.fetch[0].url === '/api/close?pin=123456', `${lang}: closing carries the PIN`);
}

// ---------------------------------------------------------------- without a PIN (access point mode)
if (pages.en) {
  const w = browser('', pages.en.html);
  vm.runInContext(pages.en.script, w.context);
  vm.runInContext('closePortal()', w.context);
  const last = w.calls.fetch[w.calls.fetch.length - 1];   // the page asks for its book list at start
  check(last && last.url === '/api/close?pin=', 'without a PIN the requests still go out (access point mode)');
}

console.log(`${failed ? 'FAILED' : 'OK'}: ${checks - failed}/${checks} portal page checks passed`);
process.exit(failed ? 1 : 0);
