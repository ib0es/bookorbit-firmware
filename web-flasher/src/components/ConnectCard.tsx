import React from 'react';
import { useI18n } from '../context/I18nContext';
import { ConnectionStatus, DetectionResult } from '../types';
import { Usb, CheckCircle2, Loader2, Unplug, RotateCcw } from 'lucide-react';

interface ConnectCardProps {
  status: ConnectionStatus;
  detection: DetectionResult;
  filterUsb: boolean;
  setFilterUsb: (v: boolean) => void;
  onConnect: () => void;
  onDisconnect: () => void;
  onReset?: () => void;
}

export const ConnectCard: React.FC<ConnectCardProps> = ({
  status,
  detection,
  filterUsb,
  setFilterUsb,
  onConnect,
  onDisconnect,
  onReset
}) => {
  const { t } = useI18n();
  const isConnected = status === 'connected' || status === 'flashing';
  const isConnecting = status === 'connecting';
  const isFlashing = status === 'flashing';

  return (
    <div className={`p-6 rounded-2xl border transition-all ${
      isConnected
        ? 'bg-blue-950/25 border-blue-500/40 shadow-xl shadow-blue-950/30'
        : 'bg-[#0b112c] border-[#17234d]'
    }`}>
      <div className="flex items-center gap-2 mb-4">
        <span className="text-xs font-bold uppercase tracking-wider text-cyan-400 bg-cyan-500/10 border border-cyan-500/20 px-2.5 py-1 rounded-md">
          {t('step1_badge')}
        </span>
      </div>

      {!isConnected ? (
        <div className="flex flex-col items-center text-center py-2">
          <button
            onClick={onConnect}
            disabled={isConnecting}
            className="flex items-center justify-center gap-2.5 w-full sm:w-auto px-8 py-3.5 bg-blue-600 hover:bg-blue-500 disabled:bg-blue-900 disabled:opacity-70 text-white font-semibold rounded-xl shadow-lg shadow-blue-950/30 transition-all active:scale-[0.98] cursor-pointer"
          >
            {isConnecting ? (
              <>
                <Loader2 className="w-5 h-5 animate-spin" />
                <span>{t('connecting')}</span>
              </>
            ) : (
              <>
                <Usb className="w-5 h-5" />
                <span>{t('btn_connect')}</span>
              </>
            )}
          </button>

          <p className="text-xs text-[#8a9fc2] mt-3">
            {t('connect_hint')}
          </p>

          <label className="flex items-center gap-2 text-xs text-[#8a9fc2] hover:text-slate-200 mt-4 cursor-pointer select-none">
            <input
              type="checkbox"
              checked={filterUsb}
              onChange={(e) => setFilterUsb(e.target.checked)}
              className="rounded bg-[#06091a] border-[#17234d] text-blue-500 focus:ring-blue-500/20 focus:ring-offset-0"
            />
            <span>{t('filter_ports_label')}</span>
          </label>
        </div>
      ) : (
        <div className="flex flex-col sm:flex-row items-start sm:items-center justify-between gap-4 py-1">
          <div className="flex items-center gap-3.5 min-w-0 flex-1">
            <div className="p-2.5 bg-blue-500/10 border border-blue-500/30 rounded-xl text-cyan-400 flex-shrink-0">
              <CheckCircle2 className="w-6 h-6" />
            </div>
            <div className="min-w-0">
              <div className="font-bold text-white text-base leading-tight">
                {detection.exactMatchName
                  ? t('connected_chip', { CHIP: `${detection.exactMatchName} (${detection.chipFamily})` })
                  : t('connected_chip', { CHIP: detection.chipFamily })}
              </div>
              <div className="text-xs text-[#8a9fc2] mt-1 font-mono">
                ROM: {detection.chipFamily}
                {detection.mac && ` · MAC: ${detection.mac}`}
                {detection.exactMatchName && ` · Verified: ${detection.exactMatchName}`}
              </div>
            </div>
          </div>

          <div className="flex items-center gap-2 flex-shrink-0">
            {onReset && (
              <button
                onClick={onReset}
                disabled={isFlashing}
                title={t('btn_restart')}
                className={`flex items-center gap-1.5 px-3 py-2 border border-[#17234d] text-xs font-semibold rounded-lg transition-colors ${
                  isFlashing
                    ? 'opacity-40 cursor-not-allowed text-[#8a9fc2]'
                    : 'hover:border-blue-500/50 text-blue-400 hover:text-blue-300 cursor-pointer'
                }`}
              >
                <RotateCcw className="w-4 h-4" />
                <span>{t('btn_restart')}</span>
              </button>
            )}

            <button
              onClick={onDisconnect}
              disabled={isFlashing}
              className={`flex items-center gap-1.5 px-3 py-2 border border-[#17234d] text-xs font-semibold rounded-lg transition-colors ${
                isFlashing
                  ? 'opacity-40 cursor-not-allowed text-[#8a9fc2]'
                  : 'hover:border-[#23366f] text-[#8a9fc2] hover:text-white cursor-pointer'
              }`}
            >
              <Unplug className="w-4 h-4" />
              <span>{t('btn_disconnect')}</span>
            </button>
          </div>
        </div>
      )}
    </div>
  );
};
