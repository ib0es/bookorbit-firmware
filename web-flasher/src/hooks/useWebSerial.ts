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
      portRef.current = port;
      const nativeUsb = port.getInfo().usbVendorId === ESPRESSIF_VID;
      const transport = new Transport(port, false);
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
        terminal,
      });
      loaderRef.current = loader;

      addLog('Connecting to ROM bootloader...');
      const chipDesc = await loader.main();
      addLog(`Detected chip: ${chipDesc}`);
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
      setDetection(res);
      setStatus('connected');
      addLog(`Connected: ${chipFamily} (MAC: ${mac || 'unknown'})${exact ? ` · model: ${DEVICES[exact].name}` : ''}`);
      return res;
    } catch (err) {
      console.error(err);
      addLog(`Connection failed: ${errText(err)}`);
      await releasePort();
      setDetection(EMPTY_DETECTION);
      setStatus('error');
      return null;
    }
  }, [addLog, releasePort]);

  const disconnect = useCallback(async () => {
    await releasePort();
    markDisconnected();
    addLog('Reader disconnected.');
  }, [addLog, releasePort, markDisconnected]);

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
    markDisconnected();
    addLog('Port released. The reader is booting.');
  }, [addLog, releasePort, markDisconnected]);

  const flash = useCallback(async ({ image, device, mode, config }: FlashRequest): Promise<void> => {
    const loader = loaderRef.current;
    if (!loader) throw new FlasherError('err_not_connected');

    const connectedChip: string = loader.chip.CHIP_NAME;
    if (connectedChip !== device.chip) {
      throw new FlasherError('err_chip_mismatch', { MODEL: device.name, MODEL_CHIP: device.chip, CHIP: connectedChip });
    }

    // Validate everything before touching the flash.
    const info = validateMergedImage(image, device.chip);
    const entries = buildConfigEntries(config);

    setStatus('flashing');
    setPhase('preparing');
    setProgress(0);

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
        const onDevice = parsePartitionTable(await loader.readFlash(PARTITION_TABLE_OFFSET, 0xc00));
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
            existingNvs = decodeNVS(await loader.readFlash(nvs.offset, nvs.size));
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
      setPhase('idle');
      setStatus('connected');
      throw err;
    }

    setPhase('restarting');
    await resetDevice();
  }, [addLog, resetDevice]);

  return { status, phase, logs, progress, detection, connect, disconnect, resetDevice, flash, clearLogs };
}
