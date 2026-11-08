import React, { useState, useEffect } from 'react';
import { AlertTriangle } from 'lucide-react';
import { useI18n } from './context/I18nContext';
import { useWebSerial } from './hooks/useWebSerial';
import { DeviceKey, GitHubRelease, DeviceInitialConfig } from './types';
import { Header } from './components/Header';
import { ConnectCard } from './components/ConnectCard';
import { DeviceGrid } from './components/DeviceGrid';
import { FlashActionBox } from './components/FlashActionBox';
import { FlashConsole } from './components/FlashConsole';
import { StepsGuide } from './components/StepsGuide';
import { FaqSection } from './components/FaqSection';
import { Footer } from './components/Footer';
import { DEVICES } from './constants/devices';
import { FlasherError } from './services/firmwareImage';
import { loadReleases, resolveFirmwareUrls } from './services/releases';

const isBin = (name: string) => name.toLowerCase().endsWith('.bin');

export const App: React.FC = () => {
  const { t } = useI18n();
  const { status, phase, logs, progress, detection, connect, disconnect, resetDevice, flash, clearLogs } = useWebSerial();

  const [selectedDevice, setSelectedDevice] = useState<DeviceKey | null>(null);
  const [selectedRelease, setSelectedRelease] = useState<string>('latest');
  const [releases, setReleases] = useState<GitHubRelease[]>([]);
  const [filterUsb, setFilterUsb] = useState<boolean>(true);
  const [doErase, setDoErase] = useState<boolean>(true);
  const [flashSuccess, setFlashSuccess] = useState<boolean>(false);
  const [flashError, setFlashError] = useState<string | null>(null);

  const [initialConfig, setInitialConfig] = useState<DeviceInitialConfig>({
    serverUrl: '', username: '', password: '', wifiSsid: '', wifiPass: '',
  });

  const [customFileBuffer, setCustomFileBuffer] = useState<Uint8Array | null>(null);
  const [customFileName, setCustomFileName] = useState<string>('');
  const [customFileSizeMb, setCustomFileSizeMb] = useState<string>('');

  const isWebSerialSupported = typeof navigator !== 'undefined' && 'serial' in navigator;
  const isFlashing = status === 'flashing';

  useEffect(() => {
    loadReleases().then(setReleases);
  }, []);

  // Closing the tab mid-write leaves the reader without a bootable firmware.
  useEffect(() => {
    if (!isFlashing) return;
    const onBeforeUnload = (e: BeforeUnloadEvent) => {
      e.preventDefault();
      e.returnValue = '';
    };
    window.addEventListener('beforeunload', onBeforeUnload);
    return () => window.removeEventListener('beforeunload', onBeforeUnload);
  }, [isFlashing]);

  const handleConnect = async () => {
    setFlashSuccess(false);
    setFlashError(null);
    const res = await connect(filterUsb);
    if (!res) return;

    // Book Orbit already installed -> update keeping settings; otherwise clean install.
    setDoErase(!res.isBookOrbitInstalled);

    if (res.exactMatchKey) {
      setSelectedDevice(res.exactMatchKey);
    } else {
      // Never guess between several models on the same chip: let the user choose.
      const compatible = (Object.keys(DEVICES) as DeviceKey[]).filter((k) => DEVICES[k].chip === res.chipFamily);
      setSelectedDevice((cur) => (cur && compatible.includes(cur) ? cur : compatible.length === 1 ? compatible[0] : null));
    }
  };

  const handleFileSelect = (file: File) => {
    if (!file || isFlashing) return;
    if (!isBin(file.name)) {
      setFlashError(t('custom_file_invalid'));
      return;
    }
    setFlashError(null);
    setCustomFileName(file.name);
    setCustomFileBuffer(null);
    setSelectedRelease('custom');
    file.arrayBuffer().then((buf) => {
      const bytes = new Uint8Array(buf);
      setCustomFileBuffer(bytes);
      setCustomFileSizeMb((bytes.length / (1024 * 1024)).toFixed(2));
    });
  };

  const handlePageDragOver = (e: React.DragEvent) => e.preventDefault();
  const handlePageDrop = (e: React.DragEvent) => {
    e.preventDefault();
    const file = e.dataTransfer.files?.[0];
    if (file && isBin(file.name)) handleFileSelect(file);
  };

  const translateError = (err: unknown) =>
    err instanceof FlasherError ? t(err.key, err.params) : err instanceof Error ? err.message : String(err);

  const handleFlash = async () => {
    setFlashSuccess(false);
    setFlashError(null);
    if (!selectedDevice) {
      setFlashError(t('err_select_model'));
      return;
    }
    const dev = DEVICES[selectedDevice];

    try {
      let image: Uint8Array;
      if (selectedRelease === 'custom') {
        if (!customFileBuffer) throw new FlasherError('custom_file_missing');
        image = customFileBuffer;
      } else {
        const urls = resolveFirmwareUrls(selectedDevice, selectedRelease, releases);
        if (!urls) throw new FlasherError('err_release_missing', { MODEL: dev.name });
        const resp = await fetch(urls.merged);
        if (!resp.ok) throw new FlasherError('err_download', { URL: urls.merged, STATUS: resp.status });
        image = new Uint8Array(await resp.arrayBuffer());
      }

      await flash({ image, device: dev, mode: doErase ? 'clean' : 'update', config: initialConfig });
      setFlashSuccess(true);
    } catch (err) {
      setFlashError(translateError(err));
    }
  };

  return (
    <div onDragOver={handlePageDragOver} onDrop={handlePageDrop} className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 py-8 space-y-8">
      <Header />

      {!isWebSerialSupported && (
        <div className="flex items-start gap-3 p-4 bg-amber-500/10 border border-amber-500/30 rounded-xl text-amber-200 text-xs shadow-md">
          <AlertTriangle className="w-5 h-5 text-amber-400 flex-shrink-0 mt-0.5" />
          <div>
            <div className="font-bold text-amber-300">{t('unsupported_title')}</div>
            <div className="mt-1 text-amber-200/80 leading-relaxed">{t('unsupported_desc')}</div>
          </div>
        </div>
      )}

      <div className="grid grid-cols-1 lg:grid-cols-12 gap-8 items-start">
        <div className="lg:col-span-7 xl:col-span-8 space-y-6">
          <ConnectCard
            status={status}
            detection={detection}
            filterUsb={filterUsb}
            setFilterUsb={setFilterUsb}
            onConnect={handleConnect}
            onDisconnect={disconnect}
            onReset={resetDevice}
          />

          <div className="bg-[#0b112c] border border-[#17234d] rounded-2xl p-6 space-y-6 shadow-xl">
            <DeviceGrid
              selectedDevice={selectedDevice}
              onSelectDevice={setSelectedDevice}
              detection={detection}
              disabled={isFlashing}
            />

            <FlashActionBox
              status={status}
              phase={phase}
              selectedDevice={selectedDevice}
              selectedRelease={selectedRelease}
              onSelectRelease={setSelectedRelease}
              releases={releases}
              customFileName={customFileName}
              customFileSizeMb={customFileSizeMb}
              onFileSelect={handleFileSelect}
              initialConfig={initialConfig}
              onInitialConfigChange={setInitialConfig}
              doErase={doErase}
              setDoErase={setDoErase}
              isBookOrbitInstalled={detection.isBookOrbitInstalled}
              progress={progress}
              flashSuccess={flashSuccess}
              flashError={flashError}
              onFlash={handleFlash}
            />
          </div>

          <FlashConsole logs={logs} onClear={clearLogs} />
        </div>

        <div className="lg:col-span-5 xl:col-span-4 space-y-6">
          <StepsGuide />
          <FaqSection />
        </div>
      </div>

      <Footer />
    </div>
  );
};
