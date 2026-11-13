import type { ESPLoader } from 'esptool-js';
import { md5Hex } from './firmwareImage';

/*
 * Stub-loader flash read done the way esptool does it (see esptool's serial
 * protocol docs, "Reading Flash"):
 *   1. SPI_SET_PARAMS with the real flash size (esptool sends it before every
 *      read; esptool-js never does),
 *   2. READ_FLASH(offset, size, 0x1000-byte packets, 64 packets in flight),
 *   3. ack every packet with the byte count received so far,
 *   4. read the 16-byte MD5 digest that follows the data and check it
 *      (esptool-js leaves it unread).
 */
const ESP_SPI_SET_PARAMS = 0x0b;
const ESP_READ_FLASH = 0xd2;
const PACKET_SIZE = 0x1000;
const MAX_IN_FLIGHT = 64;

const u32 = (...words: number[]) => {
  const out = new Uint8Array(words.length * 4);
  const view = new DataView(out.buffer);
  words.forEach((w, i) => view.setUint32(i * 4, w >>> 0, true));
  return out;
};

const toHex = (b: Uint8Array) => Array.from(b, (x) => x.toString(16).padStart(2, '0')).join('');

const SIZES: Record<string, number> = {
  '1MB': 1 << 20,
  '2MB': 2 << 20,
  '4MB': 4 << 20,
  '8MB': 8 << 20,
  '16MB': 16 << 20,
  '32MB': 32 << 20,
};

const prepared = new WeakSet<ESPLoader>();

/** SPI_SET_PARAMS once per loader (stub) session. */
export async function prepareFlash(loader: ESPLoader): Promise<void> {
  if (prepared.has(loader)) return;
  const detected = await loader.detectFlashSize();
  const size = (detected && SIZES[detected]) || SIZES['4MB'];
  await loader.checkCommand('set SPI params', ESP_SPI_SET_PARAMS, u32(0, size, 64 * 1024, 4 * 1024, 256, 0xffff));
  prepared.add(loader);
}

export async function readFlash(
  loader: ESPLoader,
  offset: number,
  size: number,
  timeoutMs = 5000,
): Promise<Uint8Array> {
  await prepareFlash(loader);
  await loader.checkCommand('read flash', ESP_READ_FLASH, u32(offset, size, PACKET_SIZE, MAX_IN_FLIGHT));
  const transport = loader.transport;
  const data = new Uint8Array(size);
  let got = 0;
  while (got < size) {
    const packet = await transport.read(timeoutMs);
    if (!packet.length) continue;
    const expected = Math.min(PACKET_SIZE, size - got);
    if (packet.length !== expected) {
      // Bytes were lost on the way (serial buffer overrun): the stub would
      // wait for an ack it never gets, so fail loudly instead.
      throw new Error(`read flash: packet of ${packet.length} bytes, expected ${expected} (serial data lost)`);
    }
    data.set(packet, got);
    got += packet.length;
    await transport.write(u32(got));
  }
  const digest = await transport.read(timeoutMs);
  if (digest.length !== 16) throw new Error(`read flash: bad digest packet (${digest.length} bytes)`);
  if (toHex(digest) !== md5Hex(data)) throw new Error('read flash: MD5 mismatch');
  return data;
}
