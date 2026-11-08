import { useState, useRef, useCallback, useEffect } from 'react';
import { ESPLoader, Transport } from 'esptool-js';
import type { FlashSizeValues, IEspLoaderTerminal } from 'esptool-js';
import { decodeNVS } from '@m1kad0/esp-nvs-utils';
import type { NVSNamespaces } from '@m1kad0/esp-nvs-utils';
import {
  ConnectionStatus, DetectionResult, DeviceInitialConfig, DeviceInfo, DeviceKey, FlashMode, FlashPhase, EMPTY_DETECTION,
} from '../types';
import { DEVICES } from '../constants/devices';
import { probeC3Board, probeS3Board, scanInstalledFirmware } from '../services/hardwareProbe';
import {
  FlasherError, md5Hex, PARTITION_TABLE_OFFSET, parsePartitionTable, samePartitionTables, validateMergedImage,
} from '../services/firmwareImage';
import { buildConfigEntries, buildNvsPartition } from '../services/nvsConfig';
import { readFlash } from '../services/flashRead';

const ESPRESSIF_VID = 0x303a;

const PORT_FILTERS: SerialPortFilter[] = [
  { usbVendorId: ESPRESSIF_VID }, // ESP32-C3 / ESP32-S3 native USB
  { usbVendorId: 0x1a86 },        // WCH CH340/CH341
  { usbVendorId: 0x10c4 },        // Silicon Labs CP210x
  { usbVendorId: 0x0403 },        // FTDI
];

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
const errText = (e: unknown) => (e instanceof Error ? e.message : String(e));

/**
 * Reset into the application (EN pulse with IO0/boot released).
 * Works for USB-Serial/JTAG and for classic UART bridges. Note: esptool-js's
 * UsbJtagSerialReset / ClassicReset enter the *bootloader* and must not be used here.
 */
async function hardResetToApp(transport: Transport) {
  await transport.setDTR(false);
  await transport.setRTS(true);
  await sleep(100);
  await transport.setRTS(false);
  await sleep(50);
}

/**
 * The port of the reader found by connect(). A reset re-enumerates native USB,
 * so the SerialPort object may have been replaced: look it up again among the
 * ports this page already has permission for (no chooser needed).
 */
async function findReaderPort(known: SerialPort): Promise<SerialPort> {
  const info = known.getInfo();
  const ports = await navigator.serial.getPorts();
  if (ports.includes(known)) return known;
  const same = ports.filter((p) => {
    const i = p.getInfo();
    return i.usbVendorId === info.usbVendorId && i.usbProductId === info.usbProductId;
  });
  if (same.length === 0) throw new Error('reader not found — check the cable and press Connect again');
  return same[0];
}

export interface FlashRequest {
  image: Uint8Array;
  device: DeviceInfo;
  mode: FlashMode;
  config: DeviceInitialConfig;
}

export function useWebSerial() {
  const [status, setStatus] = useState<ConnectionStatus>('disconnected');
  const [phase, setPhase] = useState<FlashPhase>('idle');
  const [logs, setLogs] = useState<string[]>([]);
  const [progress, setProgress] = useState<number>(0);
  const [detection, setDetection] = useState<DetectionResult>(EMPTY_DETECTION);

  const portRef = useRef<SerialPort | null>(null);
  const transportRef = useRef<Transport | null>(null);
  const loaderRef = useRef<ESPLoader | null>(null);
  /**
   * The reader found by connect(). It is not kept in download mode: after the
   * detection it is reset back into its firmware (so unplugging it is harmless)
   * and flash() opens it again through this port.
   */
  const knownPortRef = useRef<SerialPort | null>(null);
  const detectionRef = useRef<DetectionResult>(EMPTY_DETECTION);

  const addLog = useCallback((msg: string) => {
    setLogs((prev) => [...prev, msg]);
    console.log(msg);
  }, []);

  const clearLogs = useCallback(() => setLogs([]), []);

  /** Closes the port whatever state the transport is in, and forgets it. */
  const releasePort = useCallback(async () => {
    const transport = transportRef.current;
    const port = portRef.current;
    transportRef.current = null;
    loaderRef.current = null;
    portRef.current = null;
    try {
      if (transport) await transport.disconnect();
      else if (port) await port.close();
    } catch {
      try { await port?.close(); } catch { /* already closed */ }
    }
  }, []);

  const markDisconnected = useCallback(() => {
    setDetection(EMPTY_DETECTION);
    setProgress(0);
    setPhase('idle');
    setStatus('disconnected');
  }, []);

  // Cable pulled / reader rebooted on its own.
  useEffect(() => {
    const serial = navigator.serial;
    if (!serial) return;
    const onDisconnect = (e: Event) => {
      if (portRef.current && e.target === portRef.current) {
        transportRef.current = null;
        loaderRef.current = null;
        portRef.current = null;
        addLog('USB device disconnected.');
        markDisconnected();
      }
    };
    serial.addEventListener('disconnect', onDisconnect);
    return () => serial.removeEventListener('disconnect', onDisconnect);
  }, [addLog, markDisconnected]);

  /** Opens `port`, enters the ROM bootloader, runs the stub and attaches the flash. */
  const openLoader = useCallback(async (port: SerialPort) => {
    portRef.current = port;
    const nativeUsb = port.getInfo().usbVendorId === ESPRESSIF_VID;
    // ?debug in the page URL: log every serial packet to the browser console.
    const tracing = new URLSearchParams(window.location.search).has('debug');
    const transport = new Transport(port, tracing);
    transportRef.current = transport;

    const terminal: IEspLoaderTerminal = {
      clean: () => {},
      writeLine: (data: string) => addLog(data),
      write: (data: string) => addLog(data),
    };
    const loader = new ESPLoader({
      transport,
      baudrate: nativeUsb ? 115200 : 921600, // baud is irrelevant on native USB
      romBaudrate: 115200,
      // Web Serial's default receive buffer is 255 bytes. A stub flash read
      // sends 4 KB packets in one burst; the overflow is dropped silently
      // (esptool-js treats BufferOverrunError as recoverable), so reads came
      // back short and the stub then waited forever for the right ack.
      serialOptions: { bufferSize: 64 * 1024 },
      terminal,
    });
    loaderRef.current = loader;

    addLog('Connecting to ROM bootloader...');
    const chipDesc = await loader.main();
    addLog(`Detected chip: ${chipDesc}`);
    // Attach the SPI flash before any flash read/write, as esptool does
    // (esptool-js never sends SPI_ATTACH itself).
    await loader.flashSpiAttach(0);
    return { loader, transport, nativeUsb };
  }, [addLog]);

  /** Resets the reader into its firmware (if a session is open) and closes the port. */
  const leaveDownloadMode = useCallback(async (transport: Transport | null) => {
    if (transport) {
      try { await hardResetToApp(transport); } catch { /* port already gone */ }
    }
    await releasePort();
  }, [releasePort]);

  const connect = useCallback(async (filterUsbOnly: boolean): Promise<DetectionResult | null> => {
    const serial = navigator.serial;
    if (!serial) {
      addLog('Web Serial is not supported in this browser.');
      setStatus('error');
      return null;
    }

    setStatus('connecting');
    setProgress(0);

    let port: SerialPort;
    try {
      port = await serial.requestPort({ filters: filterUsbOnly ? PORT_FILTERS : [] });
    } catch (err) {
      // User closed the chooser: not an error.
      if (err instanceof DOMException && err.name === 'NotFoundError') {
        setStatus('disconnected');
      } else {
        addLog(`Connection failed: ${errText(err)}`);
        setStatus('error');
      }
      return null;
    }

    try {
      const { loader, transport, nativeUsb } = await openLoader(port);
      const chipFamily: string = loader.chip.CHIP_NAME;

      // 1) What firmware is installed (exact build name, if it is Book Orbit)?
      const sig = await scanInstalledFirmware(loader, addLog);

      // 2) What does the hardware say?
      let probed: DeviceKey | null = null;
      if (chipFamily === 'ESP32-C3') {
        if (nativeUsb) probed = await probeC3Board(loader, addLog);
        else addLog('Skipping I2C probe: GPIO20 is the UART RX line on bridge-connected boards.');
      } else if (chipFamily === 'ESP32-S3') {
        probed = await probeS3Board(loader, addLog);
      } else {
        addLog(`⚠ ${chipFamily} is not used by any supported reader.`);
      }

      let exact: DeviceKey | null = probed ?? sig.board;
      let modelConflict = false;
      if (probed && sig.board && probed !== sig.board) {
        modelConflict = true;
        exact = null;
        addLog(`⚠ Hardware looks like ${DEVICES[probed].name}, but installed firmware is for ${DEVICES[sig.board].name}. Please choose the model manually.`);
      }
      if (exact && DEVICES[exact].chip !== chipFamily) exact = null;

      let mac = '';
      try {
        mac = await loader.chip.readMac(loader);
      } catch {
        /* optional */
      }

      const res: DetectionResult = {
        exactMatchKey: exact,
        exactMatchName: exact ? DEVICES[exact].name : null,
        chipFamily,
        mac,
        isBookOrbitInstalled: sig.isBookOrbit,
        modelConflict,
      };
      addLog(`Connected: ${chipFamily} (MAC: ${mac || 'unknown'})${exact ? ` · model: ${DEVICES[exact].name}` : ''}`);

      // Hand the reader back to its firmware right away: nothing is written
      // until Install is pressed, and a reader left in download mode would
      // stay frozen after the cable is pulled.
      await leaveDownloadMode(transport);
      knownPortRef.current = port;
      detectionRef.current = res;
      addLog('Reader is back in its firmware; it switches to flashing mode when you press Install.');
      setDetection(res);
      setStatus('connected');
      return res;
    } catch (err) {
      console.error(err);
      addLog(`Connection failed: ${errText(err)}`);
      await leaveDownloadMode(transportRef.current);
      knownPortRef.current = null;
      setDetection(EMPTY_DETECTION);
      setStatus('error');
      return null;
    }
  }, [addLog, openLoader, leaveDownloadMode]);

  const disconnect = useCallback(async () => {
    await leaveDownloadMode(transportRef.current);
    knownPortRef.current = null;
    detectionRef.current = EMPTY_DETECTION;
    markDisconnected();
    addLog('Reader disconnected.');
  }, [addLog, leaveDownloadMode, markDisconnected]);

  const resetDevice = useCallback(async () => {
    addLog('\n🔄 Restarting reader into firmware...');
    const transport = transportRef.current;
    if (transport) {
      try {
        await hardResetToApp(transport);
      } catch (err) {
        addLog(`Reset signal failed: ${errText(err)} — press the reset button on the reader.`);
      }
    }
    await releasePort();
    knownPortRef.current = null;
    detectionRef.current = EMPTY_DETECTION;
    markDisconnected();
    addLog('Port released. The reader is booting.');
  }, [addLog, releasePort, markDisconnected]);

  const flash = useCallback(async ({ image, device, mode, config }: FlashRequest): Promise<void> => {
    const detected = detectionRef.current;
    if (!knownPortRef.current || !detected.chipFamily) throw new FlasherError('err_not_connected');
    if (detected.chipFamily !== device.chip) {
      throw new FlasherError('err_chip_mismatch', { MODEL: device.name, MODEL_CHIP: device.chip, CHIP: detected.chipFamily });
    }

    // Validate everything before touching the reader.
    const info = validateMergedImage(image, device.chip);
    const entries = buildConfigEntries(config);

    setStatus('flashing');
    setPhase('preparing');
    setProgress(0);

    let loader: ESPLoader;
    try {
      addLog('\nSwitching the reader into flashing mode...');
      loader = (await openLoader(await findReaderPort(knownPortRef.current))).loader;
      const mac = await loader.chip.readMac(loader).catch(() => '');
      if (detected.mac && mac && mac !== detected.mac) {
        throw new Error(`a different reader is connected (MAC ${mac}, detected ${detected.mac}); press Connect again`);
      }
    } catch (err) {
      console.error(err);
      addLog(`\n❌ Could not reach the reader: ${errText(err)}`);
      await leaveDownloadMode(transportRef.current);
      setPhase('idle');
      setStatus('connected');
      throw err;
    }

    try {
      addLog('\n========================================');
      addLog(`Preparing ${device.name} firmware (${(image.length / 1024 / 1024).toFixed(2)} MB)...`);

      const flashSize = await loader.detectFlashSize();
      if (flashSize) {
        const have = loader.flashSizeBytes(flashSize as FlashSizeValues);
        addLog(`Flash size: ${flashSize}`);
        if (have > 0 && have < info.requiredFlashBytes) {
          throw new FlasherError('err_flash_too_small', {
            HAVE: flashSize,
            NEED: `${Math.ceil(info.requiredFlashBytes / 1024 / 1024)}MB`,
          });
        }
      } else {
        addLog('⚠ Could not detect flash size, continuing.');
      }

      const { nvs } = info;
      let preserveNvs = mode === 'update';
      if (preserveNvs) {
        const onDevice = parsePartitionTable(await readFlash(loader, PARTITION_TABLE_OFFSET, 0xc00));
        if (!samePartitionTables(onDevice, info.partitions)) {
          addLog('Partition layout on the reader differs from this build: settings in NVS will be reset.');
          preserveNvs = false;
        }
      }

      const fileArray: Array<{ data: Uint8Array; address: number }> = [];
      let existingNvs: NVSNamespaces = {};

      if (preserveNvs) {
        addLog(`Update mode: keeping settings in NVS (0x${nvs.offset.toString(16)}–0x${(nvs.offset + nvs.size).toString(16)}).`);
        fileArray.push({ data: image.slice(0, nvs.offset), address: 0 });
        const tail = nvs.offset + nvs.size;
        if (image.length > tail) fileArray.push({ data: image.slice(tail), address: tail });
        if (entries.length > 0) {
          try {
            existingNvs = decodeNVS(await readFlash(loader, nvs.offset, nvs.size));
          } catch (err) {
            addLog(`⚠ Existing NVS could not be read (${errText(err)}); it will contain only the new settings.`);
          }
        }
      } else {
        fileArray.push({ data: image, address: 0 });
      }

      if (entries.length > 0) {
        addLog(`Writing ${entries.length} setting(s) to NVS at 0x${nvs.offset.toString(16)}.`);
        fileArray.push({ data: buildNvsPartition(existingNvs, entries, nvs.size), address: nvs.offset });
      }

      const eraseAll = mode === 'clean';
      const sizes = fileArray.map((f) => f.data.length);
      const totalBytes = sizes.reduce((a, b) => a + b, 0);
      if (eraseAll) {
        setPhase('erasing');
        addLog('Erasing the entire flash chip (can take up to a minute)...');
      } else {
        setPhase('writing');
      }

      await loader.writeFlash({
        fileArray,
        flashMode: 'keep',
        flashFreq: 'keep',
        flashSize: 'keep',
        eraseAll,
        compress: true,
        calculateMD5Hash: md5Hex,
        reportProgress: (fileIndex: number, written: number, total: number) => {
          setPhase('writing');
          const done = sizes.slice(0, fileIndex).reduce((a, b) => a + b, 0) + (total > 0 ? (written / total) * sizes[fileIndex] : 0);
          setProgress(Math.min(100, Math.floor((done / totalBytes) * 100)));
        },
      });

      setProgress(100);
      addLog('\n🎉 Firmware written and verified (MD5).');
    } catch (err) {
      console.error(err);
      addLog(`\n❌ Flashing error: ${errText(err)}`);
      await leaveDownloadMode(transportRef.current);
      setPhase('idle');
      setStatus('connected');
      throw err;
    }

    setPhase('restarting');
    await resetDevice();
  }, [addLog, resetDevice, openLoader, leaveDownloadMode]);

  return { status, phase, logs, progress, detection, connect, disconnect, resetDevice, flash, clearLogs };
}
