import { DeviceKey, DeviceInfo } from '../types';

/** Error whose message is an i18n key, so the UI can show it translated. */
export class FlasherError extends Error {
  constructor(
    public key: string,
    public params: Record<string, string | number> = {},
  ) {
    super(key);
    this.name = 'FlasherError';
  }
}

/** esp_image_header_t.chip_id values (see ESP-IDF esp_app_format.h). */
export const IMAGE_CHIP_IDS: Record<DeviceInfo['chip'], number> = {
  'ESP32-C3': 5,
  'ESP32-S3': 9,
};

const ESP_IMAGE_MAGIC = 0xe9;
export const PARTITION_TABLE_OFFSET = 0x8000;
const PARTITION_ENTRY_SIZE = 32;
const PARTITION_TABLE_MAX = 0xc00;

export interface PartitionEntry {
  name: string;
  type: number;
  subtype: number;
  offset: number;
  size: number;
}

const u32 = (b: Uint8Array, o: number) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;

/** Parses an ESP partition table (from an image or raw flash read starting at the table). */
export function parsePartitionTable(table: Uint8Array): PartitionEntry[] {
  const out: PartitionEntry[] = [];
  for (let i = 0; i + PARTITION_ENTRY_SIZE <= Math.min(table.length, PARTITION_TABLE_MAX); i += PARTITION_ENTRY_SIZE) {
    if (table[i] !== 0xaa || table[i + 1] !== 0x50) break; // 0xEBEB = MD5 row, 0xFFFF = end
    let name = '';
    for (let j = 12; j < 28 && table[i + j] !== 0; j++) name += String.fromCharCode(table[i + j]);
    out.push({ name, type: table[i + 2], subtype: table[i + 3], offset: u32(table, i + 4), size: u32(table, i + 8) });
  }
  return out;
}

export function imageChipId(bin: Uint8Array): number {
  return bin[12] | (bin[13] << 8);
}

export function samePartitionTables(a: PartitionEntry[], b: PartitionEntry[]): boolean {
  return (
    a.length === b.length &&
    a.every(
      (p, i) =>
        p.name === b[i].name &&
        p.type === b[i].type &&
        p.subtype === b[i].subtype &&
        p.offset === b[i].offset &&
        p.size === b[i].size,
    )
  );
}

export interface MergedImageInfo {
  partitions: PartitionEntry[];
  nvs: PartitionEntry;
  appOffset: number;
  /** Flash bytes the partition layout needs. */
  requiredFlashBytes: number;
}

/**
 * Validates that `bin` is a full (merged) Book Orbit image for `chip`:
 * bootloader at 0x0, partition table at 0x8000, app image at 0x10000.
 * Throws FlasherError with a translatable key otherwise.
 */
export function validateMergedImage(bin: Uint8Array, chip: DeviceInfo['chip']): MergedImageInfo {
  if (bin.length < 0x10000 + 0x100 || bin[0] !== ESP_IMAGE_MAGIC) {
    throw new FlasherError('err_image_not_esp');
  }

  const expectedId = IMAGE_CHIP_IDS[chip];
  const actualId = imageChipId(bin);
  if (actualId !== expectedId) {
    const name = Object.entries(IMAGE_CHIP_IDS).find(([, id]) => id === actualId)?.[0] ?? `chip id ${actualId}`;
    throw new FlasherError('err_image_wrong_chip', { IMAGE: name, CHIP: chip });
  }

  if (bin[PARTITION_TABLE_OFFSET] !== 0xaa || bin[PARTITION_TABLE_OFFSET + 1] !== 0x50) {
    throw new FlasherError('err_image_not_merged');
  }

  const partitions = parsePartitionTable(bin.subarray(PARTITION_TABLE_OFFSET));
  const nvs = partitions.find((p) => p.type === 0x01 && p.subtype === 0x02);
  const app = partitions.find((p) => p.type === 0x00 && p.offset === 0x10000);
  if (!nvs || !app) {
    throw new FlasherError('err_image_bad_partitions');
  }
  if (bin[app.offset] !== ESP_IMAGE_MAGIC || imageChipId(bin.subarray(app.offset)) !== expectedId) {
    throw new FlasherError('err_image_not_merged');
  }

  const requiredFlashBytes = partitions.reduce((m, p) => Math.max(m, p.offset + p.size), bin.length);
  return { partitions, nvs, appOffset: app.offset, requiredFlashBytes };
}

const WIPE_SD_CAP_MARKER = new TextEncoder().encode('bo:wipe_sd=1');

/** Checks whether the app image includes the first-boot SD wipe handler. */
export function supportsSdWipe(bin: Uint8Array, appOffset = 0x10000): boolean {
  const needle = WIPE_SD_CAP_MARKER;
  const limit = bin.length - needle.length;
  for (let i = appOffset; i <= limit; i++) {
    if (bin[i] !== needle[0]) continue;
    let match = true;
    for (let j = 1; j < needle.length; j++) {
      if (bin[i + j] !== needle[j]) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

/* ---------------------------- Board signatures ---------------------------- */

/** Board profile names compiled into the app (freeink-sdk BoardConfig.h). */
const BOARD_SIGNATURES: Record<string, DeviceKey> = {
  xteink_x4: 'x4',
  xteink_x3: 'x3',
  xteink_x3_uc8279: 'x3',
  xteink_x4_pro: 'x4pro',
  xteink_x4_classic: 'x4c',
  picco: 'picco',
};

function printableStrings(bytes: Uint8Array, minLen = 4): string[] {
  const out: string[] = [];
  let cur = '';
  for (let i = 0; i < bytes.length; i++) {
    const b = bytes[i];
    if (b >= 32 && b <= 126) {
      cur += String.fromCharCode(b);
    } else {
      if (cur.length >= minLen) out.push(cur);
      cur = '';
    }
  }
  if (cur.length >= minLen) out.push(cur);
  return out;
}

export interface FirmwareSignature {
  isBookOrbit: boolean;
  board: DeviceKey | null;
}

/**
 * Looks for Book Orbit markers in the start of an app image.
 * Board names must match a whole NUL-terminated string, so "xteink_x4"
 * never matches "xteink_x4_pro".
 */
export function detectFirmwareSignature(appStart: Uint8Array): FirmwareSignature {
  const strings = printableStrings(appStart);
  const isBookOrbit = strings.some((s) => /book ?orbit/i.test(s));
  const boards = new Set<DeviceKey>();
  for (const s of strings) {
    const key = BOARD_SIGNATURES[s.trim()];
    if (key) boards.add(key);
  }
  return { isBookOrbit, board: isBookOrbit && boards.size === 1 ? [...boards][0] : null };
}

/* ---------------------------------- MD5 ---------------------------------- */

const MD5_S = [
  7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 4,
  11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
];
const MD5_K = Array.from({ length: 64 }, (_, i) => Math.floor(Math.abs(Math.sin(i + 1)) * 2 ** 32) >>> 0);

/** MD5 as lowercase hex; used by esptool-js to verify what was written. */
export function md5Hex(data: Uint8Array): string {
  const len = data.length;
  const padded = new Uint8Array((((len + 8) >> 6) + 1) << 6);
  padded.set(data);
  padded[len] = 0x80;
  const view = new DataView(padded.buffer);
  view.setUint32(padded.length - 8, (len * 8) >>> 0, true);
  view.setUint32(padded.length - 4, Math.floor(len / 0x20000000), true);

  let a0 = 0x67452301,
    b0 = 0xefcdab89,
    c0 = 0x98badcfe,
    d0 = 0x10325476;
  const m = new Uint32Array(16);
  for (let off = 0; off < padded.length; off += 64) {
    for (let i = 0; i < 16; i++) m[i] = view.getUint32(off + i * 4, true);
    let a = a0,
      b = b0,
      c = c0,
      d = d0;
    for (let i = 0; i < 64; i++) {
      let f: number, g: number;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) & 15;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) & 15;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) & 15;
      }
      const tmp = d;
      d = c;
      c = b;
      const x = (a + f + MD5_K[i] + m[g]) >>> 0;
      b = (b + ((x << MD5_S[i]) | (x >>> (32 - MD5_S[i])))) >>> 0;
      a = tmp;
    }
    a0 = (a0 + a) >>> 0;
    b0 = (b0 + b) >>> 0;
    c0 = (c0 + c) >>> 0;
    d0 = (d0 + d) >>> 0;
  }
  const hex = (n: number) =>
    Array.from({ length: 4 }, (_, i) => ((n >>> (i * 8)) & 0xff).toString(16).padStart(2, '0')).join('');
  return hex(a0) + hex(b0) + hex(c0) + hex(d0);
}
