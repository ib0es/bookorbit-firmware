import React, { useEffect, useState } from 'react';
import { useI18n } from '../context/I18nContext';
import { DeviceKey, ConnectionStatus, GitHubRelease, DeviceInitialConfig, FlashPhase } from '../types';
import { resolveFirmwareUrls } from '../services/releases';
import { DEVICES } from '../constants/devices';
import { ReleaseSelector } from './ReleaseSelector';
import { InitialConfigBox } from './InitialConfigBox';
import { Zap, Loader2, Download, CheckCircle2, AlertCircle, AlertTriangle } from 'lucide-react';

interface FlashActionBoxProps {
  status: ConnectionStatus;
  phase: FlashPhase;
  selectedDevice: DeviceKey | null;
  selectedRelease: string;
  onSelectRelease: (rel: string) => void;
  releases: GitHubRelease[];
  customFileName: string;
  customFileSizeMb: string;
  onFileSelect: (file: File) => void;
  initialConfig: DeviceInitialConfig;
  onInitialConfigChange: (cfg: DeviceInitialConfig) => void;
  doErase: boolean;
  setDoErase: (v: boolean) => void;
  wipeSd: boolean;
  setWipeSd: (v: boolean) => void;
  isBookOrbitInstalled?: boolean;
  progress: number;
  flashSuccess: boolean;
  flashError: string | null;
  onFlash: () => void;
}

export const FlashActionBox: React.FC<FlashActionBoxProps> = ({
  status,
  phase,
  selectedDevice,
  selectedRelease,
  onSelectRelease,
  releases,
  customFileName,
  customFileSizeMb,
  onFileSelect,
  initialConfig,
  onInitialConfigChange,
  doErase,
  setDoErase,
  wipeSd,
  setWipeSd,
  isBookOrbitInstalled,
  progress,
  flashSuccess,
  flashError,
  onFlash
}) => {
  const { lang, t } = useI18n();
  const [confirmingWipe, setConfirmingWipe] = useState(false);
  const dev = selectedDevice ? DEVICES[selectedDevice] : null;
  const isConnected = status === 'connected' || status === 'flashing';
  const isFlashing = status === 'flashing';

  useEffect(() => {
    if (!wipeSd || status !== 'connected') {
      setConfirmingWipe(false);
    }
  }, [wipeSd, status]);

  const handleFlashClick = () => {
    if (wipeSd && !confirmingWipe) {
      setConfirmingWipe(true);
      return;
    }
    setConfirmingWipe(false);
    onFlash();
  };

  const descText = dev ? (lang === 'ru' ? dev.desc_ru : dev.desc_en) : t('choose_model_hint');
  const downloadUrls = selectedDevice ? resolveFirmwareUrls(selectedDevice, selectedRelease, releases) : null;
  const releaseMissing = !!selectedDevice && selectedRelease.startsWith('tag:') && !downloadUrls;

  const phaseText = phase === 'erasing' ? t('phase_erasing')
    : phase === 'restarting' ? t('phase_restarting')
    : phase === 'preparing' ? t('phase_preparing')
    : `${t('flashing')} ${progress > 0 ? `${progress}%` : ''}`;

  return (
    <div className="pt-6 border-t border-[#17234d] flex flex-col items-center text-center gap-4">
      <div className={`text-xs bg-[#06091a] border px-4 py-1.5 rounded-full max-w-xl ${dev ? 'text-[#8a9fc2] border-[#17234d]' : 'text-amber-300 border-amber-500/30'}`}>
        {descText}
      </div>

      <ReleaseSelector
        selectedRelease={selectedRelease}
        onSelectRelease={onSelectRelease}
        releases={releases}
        customFileName={customFileName}
        customFileSizeMb={customFileSizeMb}
        onFileSelect={onFileSelect}
        disabled={isFlashing}
      />

      {releaseMissing && dev && (
        <div className="text-[11px] text-amber-300">{t('err_release_missing', { MODEL: dev.name })}</div>
      )}

      <InitialConfigBox
        config={initialConfig}
        onChange={onInitialConfigChange}
        disabled={isFlashing}
      />

      {confirmingWipe ? (
        <div className="w-full max-w-md p-4 bg-rose-950/40 border border-rose-500/50 rounded-xl text-left space-y-3 animate-in fade-in duration-150">
          <div className="flex items-start gap-2.5">
            <AlertTriangle className="w-5 h-5 text-rose-400 flex-shrink-0 mt-0.5" />
            <div className="space-y-1">
              <div className="text-xs font-bold text-rose-200">{t('wipe_sd_confirm_title')}</div>
              <div className="text-[11px] text-rose-200/80 leading-relaxed">{t('wipe_sd_confirm_body')}</div>
            </div>
          </div>
          <div className="flex items-center justify-end gap-2 pt-1">
            <button
              type="button"
              onClick={() => setConfirmingWipe(false)}
              className="px-3.5 py-2 text-xs font-semibold text-slate-300 hover:text-white bg-[#0b112c] border border-[#17234d] rounded-lg transition-colors cursor-pointer"
            >
              {t('wipe_sd_confirm_cancel')}
            </button>
            <button
              type="button"
              onClick={handleFlashClick}
              disabled={status !== 'connected' || !dev || releaseMissing}
              className="px-4 py-2 text-xs font-bold text-white bg-rose-600 hover:bg-rose-500 rounded-lg shadow-md transition-colors cursor-pointer disabled:opacity-50"
            >
              {t('wipe_sd_confirm_ok')}
            </button>
          </div>
        </div>
      ) : (
        <button
          onClick={handleFlashClick}
          disabled={status !== 'connected' || !dev || releaseMissing}
          className="flex items-center justify-center gap-2 px-10 py-4 bg-blue-600 hover:bg-blue-500 disabled:bg-[#101736] disabled:text-[#8a9fc2] disabled:opacity-50 text-white font-bold text-base rounded-xl shadow-lg shadow-blue-950/40 transition-all active:scale-[0.98] cursor-pointer disabled:cursor-not-allowed"
        >
          {isFlashing ? (
            <>
              <Loader2 className="w-5 h-5 animate-spin" />
              <span>{phaseText}</span>
            </>
          ) : (
            <>
              <Zap className="w-5 h-5 fill-current" />
              <span>{t('btn_flash')}</span>
            </>
          )}
        </button>
      )}

      <div className="flex flex-col items-center gap-2 max-w-md">
        <label className="flex items-center gap-2 text-xs text-[#8a9fc2] hover:text-slate-200 cursor-pointer select-none text-left">
          <input
            type="checkbox"
            checked={doErase}
            disabled={isFlashing}
            onChange={(e) => setDoErase(e.target.checked)}
            className="rounded bg-[#06091a] border-[#17234d] text-blue-500 focus:ring-blue-500/20 focus:ring-offset-0 mt-0.5 flex-shrink-0"
          />
          <span>{t('erase_label')}</span>
        </label>

        {isConnected && isBookOrbitInstalled !== undefined && (
          <div className={`text-[11px] font-medium ${
            isBookOrbitInstalled ? 'text-cyan-400' : 'text-amber-400/90'
          }`}>
            {isBookOrbitInstalled ? t('erase_hint_update') : t('erase_hint_clean')}
          </div>
        )}

        <label className={`flex items-center gap-2 text-xs cursor-pointer select-none text-left mt-1 ${
          wipeSd ? 'text-rose-300 font-medium' : 'text-[#8a9fc2] hover:text-slate-200'
        }`}>
          <input
            type="checkbox"
            checked={wipeSd}
            disabled={isFlashing}
            onChange={(e) => setWipeSd(e.target.checked)}
            className="rounded bg-[#06091a] border-[#17234d] text-rose-500 focus:ring-rose-500/20 focus:ring-offset-0 mt-0.5 flex-shrink-0"
          />
          <span>{t('wipe_sd_label')}</span>
        </label>

        {wipeSd && (
          <div className="w-full flex items-start gap-2 px-3 py-2 bg-rose-950/30 border border-rose-500/30 rounded-lg text-[11px] text-rose-200/90 text-left">
            <AlertTriangle className="w-3.5 h-3.5 text-rose-400 flex-shrink-0 mt-0.5" />
            <span>{t('wipe_sd_warning')}</span>
          </div>
        )}
      </div>

      {/* Progress Bar */}
      {isFlashing && (
        <div className="w-full max-w-md bg-[#06091a] border border-[#17234d] rounded-lg h-5 overflow-hidden p-0.5">
          <div
            className={`bg-gradient-to-r ${phase === 'erasing' ? 'animate-pulse' : ''} from-blue-600 to-cyan-400 h-full rounded transition-all duration-150 ease-linear flex items-center justify-center text-[10px] font-bold text-black`}
            style={{ width: phase === 'erasing' ? '100%' : `${progress}%`, opacity: phase === 'erasing' ? 0.35 : 1 }}
          >
            {phase === 'writing' && progress > 10 && `${progress}%`}
          </div>
        </div>
      )}

      {/* Inline Flash Success Banner */}
      {flashSuccess && (
        <div className="w-full max-w-md p-3.5 bg-blue-950/50 border border-cyan-500/50 rounded-xl text-cyan-300 text-xs font-semibold flex items-center justify-center gap-2 animate-in fade-in duration-200 shadow-lg shadow-blue-950/50">
          <CheckCircle2 className="w-4 h-4 text-cyan-400 flex-shrink-0" />
          <span>{t('flash_done')}</span>
        </div>
      )}

      {/* Inline Flash Error Banner */}
      {flashError && (
        <div className="w-full max-w-md p-3.5 bg-rose-950/40 border border-rose-500/50 rounded-xl text-rose-200 text-xs flex items-start gap-2.5 text-left animate-in fade-in duration-200">
          <AlertCircle className="w-4 h-4 text-rose-400 flex-shrink-0 mt-0.5" />
          <div>
            <div className="font-semibold text-rose-300">{t('flash_err')}</div>
            <div className="text-[11px] text-rose-300/80 mt-0.5 font-mono break-all">{flashError}</div>
          </div>
        </div>
      )}

      {/* Manual direct download link */}
      {downloadUrls && (
        <div className="flex items-center gap-2 text-xs text-[#8a9fc2] mt-2">
          <Download className="w-3.5 h-3.5" />
          <span>{t('or_manual')}</span>
          <a
            href={downloadUrls.merged}
            download
            className="text-slate-300 hover:text-cyan-400 underline font-mono transition-colors"
          >
            {selectedDevice ? `bookorbit-${selectedDevice}.bin` : 'firmware.bin'} (0x0)
          </a>
        </div>
      )}
    </div>
  );
};
