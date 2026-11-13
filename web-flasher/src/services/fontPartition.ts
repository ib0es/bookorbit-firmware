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
  return partitions.find((p) => p.type === 0x01 && (p.subtype === 0x82 || p.name === 'spiffs'));
}
