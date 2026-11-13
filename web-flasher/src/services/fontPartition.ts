import type { FontFamilyMeta } from '../types';
import { PartitionEntry } from './firmwareImage';

export const FONT_HEADER_BYTES = 4096;
export const FONT_MAGIC = 0x46424946; // "FIBF"
export const FONT_VERSION = 1;

/**
 * 32-bit FNV-1a hash matching firmware's checksumOf() in src/reader/font_store.cpp.
 */
export function fnv1a(data: Uint8Array, length: number): number {
  let s = 2166136261 >>> 0;
  for (let i = 0; i < length; i++) {
    s = (s ^ data[i]) >>> 0;
    s = Math.imul(s, 16777619) >>> 0;
  }
  return s >>> 0;
}

/**
 * Detects font style index (0: Regular, 1: Bold, 2: Italic, 3: BoldItalic).
 */
export function detectStyleFromFileName(fileName: string): number {
  const stem = fileName.replace(/\.[^/.]+$/, '').toLowerCase();
  const dash = stem.lastIndexOf('-');
  const suffix = dash >= 0 ? stem.substring(dash + 1).replace(/\s/g, '') : stem.replace(/\s/g, '');

  if (['bolditalic', 'boldoblique', 'bi'].includes(suffix)) return 3;
  if (['italic', 'oblique', 'it'].includes(suffix)) return 2;
  if (['bold'].includes(suffix)) return 1;
  return 0; // regular, book, roman, normal
}

/**
 * Builds a binary SPIFFS font partition image matching firmware's FontStore partition format:
 * - 4096-byte header:
 *   - uint32 magic (0x46424946 "FIBF")
 *   - uint32 version (1)
 *   - char name[64]
 *   - struct { uint32 offset, length } faces[4] (Regular, Bold, Italic, BoldItalic)
 *   - uint32 checksum (FNV-1a of first 104 bytes)
 *   - padded to 4096 with 0
 * - Faces starting at offset 4096, each 4-byte aligned.
 */
export function buildFontPartitionImage(name: string, faces: Array<Uint8Array | null | undefined>): Uint8Array {
  if (!faces[0] || faces[0].length === 0) {
    throw new Error('A regular font face (style 0) is required.');
  }

  const header = new Uint8Array(FONT_HEADER_BYTES);
  const view = new DataView(header.buffer);

  // magic & version
  view.setUint32(0, FONT_MAGIC, true);
  view.setUint32(4, FONT_VERSION, true);

  // name[64]
  const nameBytes = new TextEncoder().encode(name);
  if (nameBytes.length === 0 || nameBytes.length > 63)
    throw new Error('Font family name must contain 1–63 UTF-8 bytes.');
  header.set(nameBytes.subarray(0, 63), 8);

  // Calculate face offsets & lengths
  let at = FONT_HEADER_BYTES;
  for (let s = 0; s < 4; s++) {
    const face = faces[s];
    if (face && face.length > 0) {
      view.setUint32(72 + s * 8, at, true);
      view.setUint32(72 + s * 8 + 4, face.length, true);
      at += (face.length + 3) & ~3;
    }
  }

  // Checksum over first 104 bytes (magic, version, name, faces)
  const checksum = fnv1a(header, 104);
  view.setUint32(104, checksum, true);

  // Create final partition image
  const out = new Uint8Array(at);
  out.set(header, 0);

  for (let s = 0; s < 4; s++) {
    const face = faces[s];
    if (face && face.length > 0) {
      const offset = view.getUint32(72 + s * 8, true);
      out.set(face, offset);
    }
  }

  return out;
}

/**
 * Finds the SPIFFS font partition in the ESP32 partition table.
 * Standard ESP-IDF SPIFFS partition: type 0x01 (Data), subtype 0x82 (SPIFFS).
 */
export function findSpiffsPartition(partitions: PartitionEntry[]): PartitionEntry | undefined {
  return partitions.find((p) => p.type === 0x01 && p.subtype === 0x82);
}

/** Validate the same header fields as FontStore::valid in the firmware. */
export function parseInstalledFont(data: Uint8Array, partitionSize: number): string | null {
  if (data.length < 108) return null;
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  if (
    view.getUint32(0, true) !== FONT_MAGIC ||
    view.getUint32(4, true) !== FONT_VERSION ||
    view.getUint32(104, true) !== fnv1a(data, 104)
  )
    return null;
  const name = data.subarray(8, 72);
  const end = name.indexOf(0);
  if (end < 0 || view.getUint32(76, true) === 0) return null;
  for (let style = 0; style < 4; style++) {
    const offset = view.getUint32(72 + style * 8, true);
    const length = view.getUint32(76 + style * 8, true);
    if (length && (offset < FONT_HEADER_BYTES || offset + length > partitionSize)) return null;
  }
  return new TextDecoder().decode(name.subarray(0, end));
}

/** Bounds include sector erasure, so no neighbouring partition can be touched. */
export function checkFontPartition(
  partitions: PartitionEntry[],
  flashBytes: number,
  imageBytes: number,
): PartitionEntry {
  const p = findSpiffsPartition(partitions);
  if (
    !p ||
    p.offset < 0x10000 ||
    p.offset % 4096 ||
    p.size % 4096 ||
    p.size <= FONT_HEADER_BYTES ||
    !flashBytes ||
    p.offset + p.size > flashBytes
  )
    throw new Error('Invalid or missing Flash font partition.');
  if (
    partitions.some((other) => other !== p && other.offset < p.offset + p.size && other.offset + other.size > p.offset)
  )
    throw new Error('Font partition overlaps another partition.');
  if (Math.ceil(imageBytes / 4096) * 4096 > p.size) throw new Error('Font does not fit the Flash font partition.');
  return p;
}

export async function loadFontFamilyImage(family: FontFamilyMeta): Promise<Uint8Array> {
  if (isBuiltinFont(family.name))
    throw new Error('Noto Serif is built into firmware and cannot be installed separately.');
  const faces: Array<Uint8Array | null> = [null, null, null, null];
  for (const fileName of family.files) {
    const response = await fetch(`${import.meta.env.BASE_URL}fonts/${family.dir}/${fileName}`);
    if (!response.ok) throw new Error(`Failed to download ${fileName}: HTTP ${response.status}`);
    const style = detectStyleFromFileName(fileName);
    if (faces[style]) throw new Error(`Duplicate font style: ${fileName}`);
    faces[style] = new Uint8Array(await response.arrayBuffer());
  }
  return buildFontPartitionImage(family.name, faces);
}

/** Reserved header padding is erased (0xff) by firmware and zeroed by the web flasher. */
export function sameFontImage(expected: Uint8Array, actual: Uint8Array): boolean {
  if (expected.length !== actual.length || !parseInstalledFont(actual, actual.length)) return false;
  return expected.every((byte, index) => (index >= 108 && index < FONT_HEADER_BYTES) || byte === actual[index]);
}

export const FONT_CATALOG_RESERVED = 8192;
export const FONT_CATALOG_MAX = 32;
export interface FontRecord {
  offset: number;
  span: number;
}
export interface FontDirectory {
  generation: number;
  records: FontRecord[];
}
export interface StoredFont extends FontRecord {
  name: string;
  bytes: number;
}
export interface FontCatalogState extends FontDirectory {
  slot: number;
  families: StoredFont[];
  dataLimit: number;
}
const key = (name: string) => name.toLowerCase().replace(/[ _-]/g, '');
export const sameFontFamily = (a: string, b: string): boolean => !!a && !!b && key(a) === key(b);
export function fontImageSpan(image: Uint8Array): number {
  return Math.ceil(image.length / 4096) * 4096;
}
export function parseFontDirectory(data: Uint8Array, limit: number): FontDirectory | null {
  if (data.length < 276) return null;
  const v = new DataView(data.buffer, data.byteOffset, data.byteLength);
  if (
    v.getUint32(0, true) !== 0x32424946 ||
    v.getUint32(4, true) !== 2 ||
    v.getUint32(12, true) > 32 ||
    v.getUint32(272, true) !== fnv1a(data, 272)
  )
    return null;
  const records: FontRecord[] = [];
  for (let i = 0; i < v.getUint32(12, true); i++) {
    const r = { offset: v.getUint32(16 + i * 8, true), span: v.getUint32(20 + i * 8, true) };
    if (
      r.offset % 4096 ||
      r.span < 8192 ||
      r.span % 4096 ||
      r.offset + r.span > limit ||
      records.some((other) => r.offset < other.offset + other.span && other.offset < r.offset + r.span)
    )
      return null;
    records.push(r);
  }
  return { generation: v.getUint32(8, true), records };
}
export function buildFontDirectory(directory: FontDirectory, limit: number): Uint8Array {
  const data = new Uint8Array(4096).fill(255),
    v = new DataView(data.buffer);
  data.fill(0, 0, 276);
  v.setUint32(0, 0x32424946, true);
  v.setUint32(4, 2, true);
  v.setUint32(8, directory.generation >>> 0, true);
  v.setUint32(12, directory.records.length, true);
  directory.records.forEach((r, i) => {
    v.setUint32(16 + i * 8, r.offset, true);
    v.setUint32(20 + i * 8, r.span, true);
  });
  v.setUint32(272, fnv1a(data, 272), true);
  if (!parseFontDirectory(data, limit)) throw new Error('Invalid font catalog.');
  return data;
}
export function findFontSpace(records: FontRecord[], limit: number, span: number): number {
  let at = 0;
  while (at + span <= limit) {
    let next = at;
    records.forEach((r) => {
      if (at < r.offset + r.span && r.offset < at + span) next = Math.max(next, r.offset + r.span);
    });
    if (next === at) return at;
    at = next;
  }
  throw new Error('Not enough contiguous Flash space. Remove unused families first.');
}
export function fontCatalogUsage(state: FontCatalogState) {
  const used = state.records.reduce((n, r) => n + r.span, 0);
  let at = 0,
    largestFree = 0;
  for (const r of [...state.records].sort((a, b) => a.offset - b.offset)) {
    largestFree = Math.max(largestFree, r.offset - at);
    at = r.offset + r.span;
  }
  largestFree = Math.max(largestFree, state.dataLimit - at, 0);
  return { usedBytes: used, freeBytes: Math.max(0, state.dataLimit - used), largestFreeBytes: largestFree };
}
export function planFontAddition(state: FontCatalogState, image: Uint8Array) {
  const name = parseInstalledFont(image, image.length);
  if (!name) throw new Error('Invalid font image.');
  const index = state.families.findIndex((f) => sameFontFamily(f.name, name));
  if (index < 0 && state.records.length >= FONT_CATALOG_MAX)
    throw new Error('The Flash font catalog is full (32 families).');
  const span = fontImageSpan(image),
    offset = findFontSpace(state.records, state.dataLimit, span);
  const records = state.records.map((r) => ({ ...r }));
  if (index < 0) records.push({ offset, span });
  else records[index] = { offset, span };
  return { offset, span, records, generation: (state.generation + 1) >>> 0 };
}

export const isBuiltinFont = (name: string): boolean => sameFontFamily(name, 'Noto Serif');
