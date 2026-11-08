import React from 'react';
import { useI18n } from '../context/I18nContext';
import { DeviceKey, DetectionResult } from '../types';
import { DEVICES } from '../constants/devices';

interface DeviceGridProps {
  selectedDevice: DeviceKey | null;
  onSelectDevice: (key: DeviceKey) => void;
  detection: DetectionResult;
  disabled?: boolean;
}

export const DeviceGrid: React.FC<DeviceGridProps> = ({
  selectedDevice,
  onSelectDevice,
  detection,
  disabled = false
}) => {
  const { t } = useI18n();

  const isConnected = !!detection.chipFamily;

  const getCardStatus = (key: DeviceKey) => {
    if (!isConnected) return { isExact: false, isCompat: false };

    if (detection.exactMatchKey) {
      return {
        isExact: detection.exactMatchKey === key,
        isCompat: false
      };
    }

    const dev = DEVICES[key];
    const isCompat = dev.chip === detection.chipFamily;
    return { isExact: false, isCompat };
  };

  const getHintText = () => {
    if (!isConnected) return t('compat_hint_all');
    if (detection.exactMatchKey) return t('compat_hint_exact');
    if (detection.modelConflict) return t('compat_hint_conflict');
    return t('compat_hint_found');
  };

  return (
    <div className="space-y-3">
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-1">
        <h3 className="text-xs font-bold uppercase tracking-wider text-[#8a9fc2]">
          {t('step2_title')}
        </h3>
        <span className={`text-xs ${isConnected ? 'text-cyan-400 font-medium' : 'text-[#8a9fc2]'}`}>
          {getHintText()}
        </span>
      </div>

      <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-5 gap-2.5">
        {(Object.keys(DEVICES) as DeviceKey[]).map((key) => {
          const dev = DEVICES[key];
          const isSelected = selectedDevice === key;
          const { isExact, isCompat } = getCardStatus(key);

          return (
            <div
              key={key}
              role="button"
              aria-pressed={isSelected}
              aria-disabled={disabled}
              onClick={() => !disabled && onSelectDevice(key)}
              className={`relative flex flex-col items-center text-center p-3.5 rounded-xl border-2 transition-all select-none ${
                disabled ? 'cursor-not-allowed' : 'cursor-pointer'
              } ${
                isSelected
                  ? 'border-blue-500 bg-blue-950/40 shadow-lg shadow-blue-950/50 ring-2 ring-blue-500/40'
                  : 'border-[#17234d] bg-[#06091a] hover:border-[#23366f]'
              } ${
                isExact
                  ? 'border-cyan-400 bg-cyan-950/25 shadow-cyan-950/40 ring-2 ring-cyan-500/40'
                  : isCompat
                  ? 'border-blue-500/70 bg-blue-950/20'
                  : isConnected
                  ? 'opacity-65 hover:opacity-100'
                  : ''
              }`}
            >
              <div className="font-bold text-sm text-white">{dev.name}</div>
              <div className="text-[11px] text-[#8a9fc2] font-mono mt-0.5">{dev.chipSubtitle}</div>

              {isExact && (
                <span className="mt-2 text-[10px] font-bold text-white bg-blue-600 px-2 py-0.5 rounded shadow-sm">
                  {t('badge_exact')}
                </span>
              )}

              {isCompat && !isExact && (
                <span className="mt-2 text-[10px] font-bold text-white bg-blue-800/90 px-2 py-0.5 rounded shadow-sm">
                  {t('badge_compat')}
                </span>
              )}
            </div>
          );
        })}
      </div>
    </div>
  );
};
