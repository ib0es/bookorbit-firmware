import React from 'react';
import { useI18n } from '../context/I18nContext';
import { DeviceInitialConfig } from '../types';
import { Server, Wifi, ChevronDown, User, KeyRound, Globe } from 'lucide-react';

interface InitialConfigBoxProps {
  config: DeviceInitialConfig;
  onChange: (cfg: DeviceInitialConfig) => void;
  disabled?: boolean;
}

export const InitialConfigBox: React.FC<InitialConfigBoxProps> = ({ config, onChange, disabled = false }) => {
  const { t } = useI18n();

  const handleChange = (field: keyof DeviceInitialConfig, val: string) => {
    onChange({
      ...config,
      [field]: val,
    });
  };

  const hasAnyConfig = Boolean(
    config.serverUrl?.trim() || config.username?.trim() || config.password || config.wifiSsid || config.wifiPass,
  );

  return (
    <details className="group w-full max-w-md bg-[#06091a] border border-[#17234d] hover:border-[#23366f] rounded-xl overflow-hidden text-left transition-colors">
      <summary className="p-3.5 text-xs font-semibold text-slate-200 cursor-pointer select-none hover:text-cyan-400 flex items-center justify-between transition-colors">
        <div className="flex items-center gap-2">
          <Server className="w-4 h-4 text-cyan-400" />
          <span>{t('setup_title')}</span>
        </div>
        <div className="flex items-center gap-2">
          {hasAnyConfig && <span className="w-2 h-2 rounded-full bg-cyan-400 animate-pulse" />}
          <span className="text-[10px] font-mono text-[#8a9fc2] bg-blue-500/10 border border-blue-500/20 px-2 py-0.5 rounded-full">
            {t('badge_optional')}
          </span>
          <ChevronDown className="w-3.5 h-3.5 text-[#8a9fc2] group-open:rotate-180 transition-transform" />
        </div>
      </summary>

      <fieldset
        disabled={disabled}
        className="p-4 pt-1 space-y-4 border-t border-[#17234d]/60 text-xs disabled:opacity-60"
      >
        <p className="text-[11px] text-[#8a9fc2] leading-relaxed">{t('setup_subtitle')}</p>

        {/* Server Section */}
        <div className="space-y-2.5">
          <div className="flex items-center gap-1.5 text-[11px] font-bold uppercase tracking-wider text-cyan-400">
            <Globe className="w-3.5 h-3.5" />
            <span>{t('setup_server_h')}</span>
          </div>

          <div>
            <label className="block text-[11px] text-[#8a9fc2] mb-1">{t('setup_server_url')}</label>
            <input
              type="url"
              inputMode="url"
              autoComplete="off"
              spellCheck={false}
              value={config.serverUrl || ''}
              onChange={(e) => handleChange('serverUrl', e.target.value)}
              placeholder={t('setup_server_url_ph')}
              className="w-full bg-[#0b112c] border border-[#17234d] focus:border-cyan-400 rounded-lg px-3 py-2 text-xs text-white placeholder-[#8a9fc2]/40 outline-none transition-colors font-mono"
            />
          </div>

          <div className="grid grid-cols-1 sm:grid-cols-2 gap-2.5">
            <div>
              <label className="block text-[11px] text-[#8a9fc2] mb-1 flex items-center gap-1">
                <User className="w-3 h-3" />
                <span>{t('setup_user')}</span>
              </label>
              <input
                type="text"
                autoComplete="off"
                spellCheck={false}
                value={config.username || ''}
                onChange={(e) => handleChange('username', e.target.value)}
                placeholder="reader"
                className="w-full bg-[#0b112c] border border-[#17234d] focus:border-cyan-400 rounded-lg px-3 py-2 text-xs text-white placeholder-[#8a9fc2]/40 outline-none transition-colors"
              />
            </div>

            <div>
              <label className="block text-[11px] text-[#8a9fc2] mb-1 flex items-center gap-1">
                <KeyRound className="w-3 h-3" />
                <span>{t('setup_pass')}</span>
              </label>
              <input
                type="password"
                autoComplete="new-password"
                value={config.password || ''}
                onChange={(e) => handleChange('password', e.target.value)}
                placeholder="••••••••"
                className="w-full bg-[#0b112c] border border-[#17234d] focus:border-cyan-400 rounded-lg px-3 py-2 text-xs text-white placeholder-[#8a9fc2]/40 outline-none transition-colors"
              />
            </div>
          </div>
        </div>

        {/* Wi-Fi Section */}
        <div className="space-y-2.5 pt-3 border-t border-[#17234d]/60">
          <div className="flex items-center gap-1.5 text-[11px] font-bold uppercase tracking-wider text-cyan-400">
            <Wifi className="w-3.5 h-3.5" />
            <span>{t('setup_wifi_h')}</span>
          </div>

          <div className="grid grid-cols-1 sm:grid-cols-2 gap-2.5">
            <div>
              <label className="block text-[11px] text-[#8a9fc2] mb-1">{t('setup_wifi_ssid')}</label>
              <input
                type="text"
                autoComplete="off"
                spellCheck={false}
                maxLength={32}
                value={config.wifiSsid || ''}
                onChange={(e) => handleChange('wifiSsid', e.target.value)}
                placeholder="MyHomeWiFi"
                className="w-full bg-[#0b112c] border border-[#17234d] focus:border-cyan-400 rounded-lg px-3 py-2 text-xs text-white placeholder-[#8a9fc2]/40 outline-none transition-colors"
              />
            </div>

            <div>
              <label className="block text-[11px] text-[#8a9fc2] mb-1">{t('setup_wifi_pass')}</label>
              <input
                type="password"
                autoComplete="new-password"
                maxLength={63}
                value={config.wifiPass || ''}
                onChange={(e) => handleChange('wifiPass', e.target.value)}
                placeholder="••••••••"
                className="w-full bg-[#0b112c] border border-[#17234d] focus:border-cyan-400 rounded-lg px-3 py-2 text-xs text-white placeholder-[#8a9fc2]/40 outline-none transition-colors"
              />
            </div>
          </div>
        </div>
      </fieldset>
    </details>
  );
};
