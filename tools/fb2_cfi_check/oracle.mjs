// The web reader's side, run in Chromium with foliate-js itself (not in this
// repository: give the folder of fb2.js and epubcfi.js, e.g. a Book Orbit
// checkout's client/public/assets/foliate).
//
//   node oracle.mjs <foliate dir> <book.fb2> <ours.json> <job.json> <out.json>
//
// ours.json: the converted EPUB's chapters, ["<xhtml>", ...] in spine order.
// job.json: {"sampleEvery": N, "resolveTheirs": [cfi...], "resolveOurs": [cfi...]}
// out.json: {"theirs": [{cfi, text}], "ours": [{cfi, text}],
//            "resolvedTheirs": [text|null], "resolvedOurs": [text|null]}
// Samples are points in text (every N-th text node, a few characters in); a
// resolved CFI gives the text that follows it.
import { createRequire } from 'module'
import fs from 'fs'
import path from 'path'

// Playwright from the global packages (NODE_PATH), as check.py runs it
const { chromium } = createRequire(import.meta.url)('playwright')

const [dir, fb2, oursFile, jobFile, outFile] = process.argv.slice(2)
const fb2Src = fs.readFileSync(path.join(dir, 'fb2.js'), 'utf8')
const cfiSrc = fs.readFileSync(path.join(dir, 'epubcfi.js'), 'utf8')
const book = fs.readFileSync(fb2).toString('base64')
const ours = JSON.parse(fs.readFileSync(oursFile, 'utf8'))
const job = JSON.parse(fs.readFileSync(jobFile, 'utf8'))

const browser = await chromium.launch()
const page = await browser.newPage()
page.on('console', (m) => console.error('[page]', m.text()))
const out = await page.evaluate(
  async ({ fb2Src, cfiSrc, book, ours, job }) => {
    const mod = (src) => import(URL.createObjectURL(new Blob([src], { type: 'text/javascript' })))
    const { makeFB2 } = await mod(fb2Src)
    const CFI = await mod(cfiSrc)
    const bytes = Uint8Array.from(atob(book), (c) => c.charCodeAt(0))
    const fb = await makeFB2(new Blob([bytes]))
    const theirDocs = fb.sections.map((s) => s.createDocument())
    const ourDocs = ours.map((x) => new DOMParser().parseFromString(x, 'application/xhtml+xml'))

    const after = (doc, node, offset) => {
      const r = doc.createRange()
      r.setStart(node, offset)
      r.setEnd(doc.body, doc.body.childNodes.length)
      return r.toString().slice(0, 60)
    }
    const sample = (docs) => {
      const res = []
      docs.forEach((doc, index) => {
        const w = doc.createTreeWalker(doc.body, NodeFilter.SHOW_TEXT)
        let n = 0
        for (let t = w.nextNode(); t; t = w.nextNode()) {
          const s = t.nodeValue
          if (!/\S/.test(s)) continue
          if (n++ % job.sampleEvery) continue
          // a few characters in, at a non-space
          let off = Math.min(3, s.length - 1)
          while (off < s.length - 1 && /\s/.test(s[off])) off++
          const r = doc.createRange()
          r.setStart(t, off)
          r.collapse(true)
          const cfi = CFI.joinIndir(CFI.fake.fromIndex(index), CFI.fromRange(r))
          res.push({ cfi, text: after(doc, t, off) })
        }
      })
      return res
    }
    const resolve = (docs, cfis) =>
      cfis.map((cfi) => {
        try {
          if (cfi === '-') return null
          const parts = CFI.parse(cfi)
          const index = CFI.fake.toIndex((parts.parent ?? parts).shift())
          const doc = docs[index]
          if (!doc) return null
          const r = CFI.toRange(doc, parts)
          return after(doc, r.startContainer, r.startOffset)
        } catch (e) {
          return 'ERROR ' + e
        }
      })
    return {
      sections: theirDocs.length,
      theirs: sample(theirDocs),
      ours: sample(ourDocs),
      resolvedTheirs: resolve(theirDocs, job.resolveTheirs || []),
      resolvedOurs: resolve(ourDocs, job.resolveOurs || []),
    }
  },
  { fb2Src, cfiSrc, book, ours, job },
)
await browser.close()
fs.writeFileSync(outFile, JSON.stringify(out))
