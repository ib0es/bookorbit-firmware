import type { ESPLoader } from 'esptool-js';
import { detectFirmwareSignature, FirmwareSignature, PARTITION_TABLE_OFFSET, parsePartitionTable } from './firmwareImage';

type Log = (msg: string) => void;
type Chip = 'C3' | 'S3';

/*
 * Register map shared by ESP32-C3 and ESP32-S3 (TRM: GPIO matrix & IO MUX).
 * Pins must be routed to the GPIO matrix (MCU_SEL = 1) with input enabled,
 * otherwise GPIO_ENABLE has no effect and GPIO_IN reads garbage — e.g. C3 GPIO20
 * defaults to U0RXD.
 */
const GPIO_OUT_W1TC = 0x6000400c;
const GPIO_ENABLE = 0x60004020;
const GPIO_ENABLE_W1TS = 0x60004024;
const GPIO_ENABLE_W1TC = 0x60004028;
const GPIO_IN = 0x6000403c;
const gpioOutSelReg = (pin: number) => 0x60004554 + 4 * pin;
const ioMuxReg = (pin: number) => 0x60009004 + 4 * pin;

const IO_MUX_FUN_WPD = 1 << 7;
const IO_MUX_FUN_WPU = 1 << 8;
const IO_MUX_FUN_IE = 1 << 9;
const IO_MUX_MCU_SEL_MASK = 0x7 << 12;
const IO_MUX_MCU_SEL_GPIO = 0x1 << 12;

/** "Simple GPIO output" signal index and OEN_SEL bit differ between chips. */
const OUT_SEL_GPIO: Record<Chip, number> = { C3: 128 | (1 << 9), S3: 256 | (1 << 10) };

interface BusProbe {
  /** true: both lines high with internal pull-downs on (external pull-ups present). */
  pullUps: boolean | null;
  /** First address that ACKed, if any. */
  ackAddr: number | null;
}

async function probeI2cBus(
  loader: ESPLoader, chip: Chip, sda: number, scl: number, addrs: number[], log: Log
): Promise<BusProbe> {
  const pins = [sda, scl];
  const mask = (1 << sda) | (1 << scl);
  const saved = {
    iomux: await Promise.all(pins.map((p) => loader.readReg(ioMuxReg(p)))),
    outSel: await Promise.all(pins.map((p) => loader.readReg(gpioOutSelReg(p)))),
    enable: await loader.readReg(GPIO_ENABLE),
  };

  const setIoMux = async (pullDown: boolean) => {
    for (let i = 0; i < pins.length; i++) {
      let v = (saved.iomux[i] & ~IO_MUX_MCU_SEL_MASK & ~IO_MUX_FUN_WPU & ~IO_MUX_FUN_WPD) | IO_MUX_MCU_SEL_GPIO | IO_MUX_FUN_IE;
      if (pullDown) v |= IO_MUX_FUN_WPD;
      await loader.writeReg(ioMuxReg(pins[i]), v >>> 0);
    }
  };
  const readPin = async (pin: number) => ((await loader.readReg(GPIO_IN)) >>> pin) & 1;
  // Open-drain emulation: output latch stays 0, "high" = release (input), "low" = drive.
  const release = (m: number) => loader.writeReg(GPIO_ENABLE_W1TC, m);
  const drive = (m: number) => loader.writeReg(GPIO_ENABLE_W1TS, m);

  try {
    await release(mask);
    await loader.writeReg(GPIO_OUT_W1TC, mask);
    for (const p of pins) await loader.writeReg(gpioOutSelReg(p), OUT_SEL_GPIO[chip]);

    // 1) Weak internal pull-downs (~45k) lose against external 2.2k–10k pull-ups.
    await setIoMux(true);
    const sdaV = await readPin(sda);
    const sclV = await readPin(scl);
    log(`GPIO${sda}/GPIO${scl} with internal pull-downs: SDA=${sdaV} SCL=${sclV}`);
    if (sdaV !== sclV) {
      return { pullUps: null, ackAddr: null }; // inconsistent: something else on these pins
    }
    if (sdaV === 0) {
      return { pullUps: false, ackAddr: null };
    }

    // 2) Bus has pull-ups: bit-bang an address-only write and check ACK.
    await setIoMux(false);
    for (const addr of addrs) {
      // START: SDA falls while SCL high
      await release(mask);
      await drive(1 << sda);
      await drive(1 << scl);
      const byte = (addr << 1) & 0xfe;
      for (let bit = 7; bit >= 0; bit--) {
        if ((byte >> bit) & 1) await release(1 << sda); else await drive(1 << sda);
        await release(1 << scl);
        await drive(1 << scl);
      }
      await release(1 << sda);
      await release(1 << scl);
      const ack = (await readPin(sda)) === 0;
      await drive(1 << scl);
      // STOP: SDA rises while SCL high
      await drive(1 << sda);
      await release(1 << scl);
      await release(1 << sda);
      log(`I2C 0x${addr.toString(16).padStart(2, '0')}: ${ack ? 'ACK' : 'NACK'}`);
      if (ack) return { pullUps: true, ackAddr: addr };
    }
    return { pullUps: true, ackAddr: null };
  } finally {
    // Restore pin configuration exactly as the ROM left it.
    try {
      for (let i = 0; i < pins.length; i++) {
        await loader.writeReg(gpioOutSelReg(pins[i]), saved.outSel[i]);
        await loader.writeReg(ioMuxReg(pins[i]), saved.iomux[i]);
      }
      if (saved.enable & mask) await loader.writeReg(GPIO_ENABLE_W1TS, saved.enable & mask);
    } catch {
      /* best effort */
    }
  }
}

/**
 * ESP32-C3: X3 has I2C pull-ups on SDA=20/SCL=0 and a BQ27220 gauge at 0x55,
 * X4 has no pull-ups there. Returns null whenever the evidence is not clear.
 * Only safe over native USB: GPIO20 is U0RXD, so never call this through a UART bridge.
 */
export async function probeC3Board(loader: ESPLoader, log: Log): Promise<'x3' | 'x4' | null> {
  try {
    log('Probing I2C bus on ESP32-C3 (SDA=20, SCL=0)...');
    const r = await probeI2cBus(loader, 'C3', 20, 0, [0x55], log);
    if (r.pullUps === false) {
      log('No I2C pull-ups on GPIO20/GPIO0 -> Xteink X4 hardware');
      return 'x4';
    }
    if (r.ackAddr === 0x55) {
      log('BQ27220 fuel gauge answered at 0x55 -> Xteink X3 hardware');
      return 'x3';
    }
    log('I2C probe inconclusive');
  } catch (err) {
    log(`ESP32-C3 I2C probe failed (${err instanceof Error ? err.message : err})`);
  }
  return null;
}

/**
 * ESP32-S3: BOOX Picco has TMA525C touch (0x24) / SGM41562 charger (0x03) on SDA=7/SCL=8.
 * X4 Pro / X4 Classic are told apart only by the installed firmware signature.
 */
export async function probeS3Board(loader: ESPLoader, log: Log): Promise<'picco' | null> {
  try {
    log('Probing I2C bus on ESP32-S3 (SDA=7, SCL=8)...');
    const r = await probeI2cBus(loader, 'S3', 7, 8, [0x24, 0x03], log);
    if (r.ackAddr !== null) {
      log('Picco touch/charger answered -> BOOX Picco hardware');
      return 'picco';
    }
    log('I2C probe inconclusive');
  } catch (err) {
    log(`ESP32-S3 I2C probe failed (${err instanceof Error ? err.message : err})`);
  }
  return null;
}

/** Reads the start of the first app partition and looks for Book Orbit / board markers. */
export async function scanInstalledFirmware(loader: ESPLoader, log: Log): Promise<FirmwareSignature> {
  try {
    const table = parsePartitionTable(await loader.readFlash(PARTITION_TABLE_OFFSET, 0xc00));
    const app = table.find((p) => p.type === 0x00) ?? { offset: 0x10000 };
    log(`Reading firmware header at 0x${app.offset.toString(16)}...`);
    const sig = detectFirmwareSignature(await loader.readFlash(app.offset, 8192));
    if (sig.isBookOrbit) log(`Book Orbit is installed${sig.board ? ` (build: ${sig.board})` : ''}`);
    else log('No Book Orbit firmware found (factory or other firmware)');
    return sig;
  } catch (err) {
    log(`Firmware scan skipped: ${err instanceof Error ? err.message : err}`);
    return { isBookOrbit: false, board: null };
  }
}

