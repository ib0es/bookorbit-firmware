import type { ESPLoader } from 'esptool-js';
import { md5Hex } from './firmwareImage';

/*
 * Stub-loader flash read done the way esptool does it (see esptool's serial
 * protocol docs, "Reading Flash"):
 *   1. SPI_SET_PARAMS with the real flash size (esptool sends it before every
 *      read; esptool-js never does),
 *   2. independent READ_FLASH requests of at most 256 bytes,
 *   3. ack every packet with the byte count received so far,
 *   4. read the 16-byte MD5 digest that follows the data and check it
 *      (esptool-js leaves it unread).
 */
const ESP_SPI_SET_PARAMS = 0x0b;
const ESP_READ_FLASH = 0xd2;
const PACKET_SIZE = 0x100;
const MAX_IN_FLIGHT = 1;

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

const flashSizes = new WeakMap<ESPLoader, number>();

/** Configure before every read; register-based flash ID operations can change SPI state. */
export async function prepareFlash(loader: ESPLoader): Promise<void> {
  let size = flashSizes.get(loader);
  if (!size) {
    const detected = await loader.detectFlashSize();
    size = detected ? SIZES[detected] : undefined;
    if (!size) throw new Error('Cannot determine Flash size; USB read was stopped.');
    flashSizes.set(loader, size);
  }
  await loader.checkCommand('set SPI params', ESP_SPI_SET_PARAMS, u32(0, size, 64 * 1024, 4 * 1024, 256, 0xffff));
}

export async function readFlash(
  loader: ESPLoader,
  offset: number,
  size: number,
  timeoutMs = 5000,
): Promise<Uint8Array> {
  let stage = 'SPI parameters';
  let got = 0;
  try {
    await prepareFlash(loader);
    const transport = loader.transport;
    const data = new Uint8Array(size);
    // Finish each bounded request (including its digest) before issuing the next.
    // This avoids a long native USB stream and limits bursts to 256 bytes.
    while (got < size) {
      const chunkSize = Math.min(PACKET_SIZE, size - got);
      stage = `READ_FLASH response after ${got}/${size} bytes`;
      await loader.checkCommand('read flash', ESP_READ_FLASH, u32(offset + got, chunkSize, PACKET_SIZE, MAX_IN_FLIGHT));
      stage = `data packet after ${got}/${size} bytes`;
      let packet = await transport.read(timeoutMs);
      while (!packet.length) packet = await transport.read(timeoutMs);
      if (packet.length !== chunkSize) {
        throw new Error(`read flash: packet of ${packet.length} bytes, expected ${chunkSize} (serial data lost)`);
      }
      await transport.write(u32(chunkSize));
      stage = `MD5 digest after ${got}/${size} bytes`;
      const digest = await transport.read(timeoutMs);
      if (digest.length !== 16) throw new Error(`read flash: bad digest packet (${digest.length} bytes)`);
      if (toHex(digest) !== md5Hex(packet)) throw new Error('read flash: MD5 mismatch');
      data.set(packet, got);
      got += chunkSize;
      // Let Web Serial and the stub complete the previous transaction.
      await new Promise((resolve) => setTimeout(resolve, 2));
    }
    return data;
  } catch (error) {
    throw new Error(
      `Flash read at 0x${offset.toString(16)}, ${stage}: ${error instanceof Error ? error.message : String(error)}`,
    );
  }
}
