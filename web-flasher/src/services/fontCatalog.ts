import type { ESPLoader } from 'esptool-js';
import type { PartitionEntry } from './firmwareImage';
import { md5Hex } from './firmwareImage';
import { readFlash } from './flashRead';
import {
  FONT_HEADER_BYTES,
  FONT_CATALOG_RESERVED,
  parseFontDirectory,
  parseInstalledFont,
  buildFontDirectory,
  planFontAddition,
  sameFontFamily,
  isBuiltinFont,
} from './fontPartition';
import type { FontCatalogState, FontDirectory } from './fontPartition';

export async function readFontCatalog(loader: ESPLoader, partition: PartitionEntry): Promise<FontCatalogState> {
  const dataLimit = partition.size - FONT_CATALOG_RESERVED;
  if (dataLimit < 8192 || partition.size % 4096) throw new Error('Font partition is too small.');
  const a = parseFontDirectory(await readFlash(loader, partition.offset + dataLimit, 276), dataLimit);
  const b = parseFontDirectory(await readFlash(loader, partition.offset + dataLimit + 4096, 276), dataLimit);
  let legacyHeader: Uint8Array | undefined;
  let slot = -1,
    d: FontDirectory = { generation: 0, records: [] };
  if (a) {
    slot = 0;
    d = a;
  }
  if (b && (!a || ((b.generation - a.generation) | 0) > 0)) {
    slot = 1;
    d = b;
  }
  if (slot < 0) {
    const header = await readFlash(loader, partition.offset, 108);
    legacyHeader = header;
    if (parseInstalledFont(header, partition.size)) {
      const v = new DataView(header.buffer);
      let end = 4096;
      for (let i = 0; i < 4; i++)
        if (v.getUint32(76 + i * 8, true))
          end = Math.max(end, v.getUint32(72 + i * 8, true) + v.getUint32(76 + i * 8, true));
      d.records.push({ offset: 0, span: Math.ceil(end / 4096) * 4096 });
    }
  }
  const families = [];
  for (const record of d.records) {
    const header =
        record.offset === 0 && legacyHeader
          ? legacyHeader
          : await readFlash(loader, partition.offset + record.offset, 108),
      name = parseInstalledFont(header, record.span);
    if (!name) throw new Error('Invalid font record in Flash catalog.');
    const v = new DataView(header.buffer);
    let bytes = 0;
    for (let i = 0; i < 4; i++) bytes += v.getUint32(76 + i * 8, true);
    families.push({ ...record, name, bytes });
  }
  return { ...d, slot, families, dataLimit };
}
async function program(loader: ESPLoader, data: Uint8Array, address: number, onProgress?: (percent: number) => void) {
  await loader.writeFlash({
    fileArray: [{ data, address }],
    flashMode: 'keep',
    flashFreq: 'keep',
    flashSize: 'keep',
    eraseAll: false,
    compress: true,
    calculateMD5Hash: md5Hex,
    reportProgress: (_i: number, written: number, total: number) =>
      onProgress?.(Math.floor((100 * written) / Math.max(total, 1))),
  });
}
async function commit(loader: ESPLoader, p: PartitionEntry, state: FontCatalogState, d: FontDirectory) {
  const slot = state.slot === 0 ? 1 : 0,
    data = buildFontDirectory(d, state.dataLimit);
  await program(loader, data, p.offset + state.dataLimit + slot * 4096);
  const result = await readFontCatalog(loader, p);
  if (result.generation !== d.generation || result.records.length !== d.records.length)
    throw new Error('Font catalog verification failed.');
}
export async function installCatalogFont(
  loader: ESPLoader,
  p: PartitionEntry,
  image: Uint8Array,
  onProgress?: (percent: number) => void,
): Promise<boolean> {
  const state = await readFontCatalog(loader, p),
    name = parseInstalledFont(image, image.length);
  if (!name) throw new Error('Invalid font image.');
  if (isBuiltinFont(name)) throw new Error('The built-in font cannot be installed separately.');
  const existing = state.families.find((f) => sameFontFamily(f.name, name));
  if (existing && image.length <= existing.span) {
    const header = await readFlash(loader, p.offset + existing.offset, 108);
    const headerMatches = header.every((byte, index) => byte === image[index]);
    const payloadHash = headerMatches
      ? await loader.flashMd5sum(p.offset + existing.offset + FONT_HEADER_BYTES, image.length - FONT_HEADER_BYTES)
      : '';
    if (headerMatches && payloadHash === md5Hex(image.subarray(FONT_HEADER_BYTES))) {
      onProgress?.(100);
      return false;
    }
  }
  const plan = planFontAddition(state, image),
    staged = image.slice();
  staged.fill(255, 0, FONT_HEADER_BYTES);
  await program(loader, staged, p.offset + plan.offset, (percent) => onProgress?.(Math.floor(percent * 0.9)));
  await program(loader, image.subarray(0, FONT_HEADER_BYTES), p.offset + plan.offset);
  await commit(loader, p, state, plan);
  onProgress?.(100);
  return true;
}
export async function removeCatalogFont(loader: ESPLoader, p: PartitionEntry, name: string): Promise<boolean> {
  if (isBuiltinFont(name)) throw new Error('The built-in font cannot be removed.');
  const state = await readFontCatalog(loader, p),
    index = state.families.findIndex((f) => sameFontFamily(f.name, name));
  if (index < 0) return false;
  const records = state.records.filter((_r, i) => i !== index);
  await commit(loader, p, state, { records, generation: (state.generation + 1) >>> 0 });
  return true;
}
