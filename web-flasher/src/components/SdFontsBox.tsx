import React, { useEffect, useState } from 'react';
import { Type, Download, Folder, CheckCircle2, AlertCircle, HardDrive, Upload, Loader2 } from 'lucide-react';
import { useI18n } from '../context/i18n';
import { FontFamilyMeta } from '../types';
import {
  fetchFontManifest,
  installFontsToDirectory,
  installCustomFontFile,
  downloadFontsZip,
  isFileSystemAccessSupported,
} from '../services/sdCardFonts';

export const SdFontsBox: React.FC = () => {
  const { lang, t } = useI18n();
  const [families, setFamilies] = useState<FontFamilyMeta[]>([]);
  const [selectedIds, setSelectedIds] = useState<Set<string>>(new Set());
  const [statusMsg, setStatusMsg] = useState<string | null>(null);
  const [statusType, setStatusType] = useState<'info' | 'success' | 'error' | null>(null);
  const [progress, setProgress] = useState<number>(0);
  const [isInstalling, setIsInstalling] = useState<boolean>(false);

  // Custom font upload
  const [showCustom, setShowCustom] = useState<boolean>(false);
  const [customFile, setCustomFile] = useState<File | null>(null);
  const [customSubfolder, setCustomSubfolder] = useState<string>('');
  const [isCustomInstalling, setIsCustomInstalling] = useState<boolean>(false);

  const canDirectWrite = isFileSystemAccessSupported();

  useEffect(() => {
    fetchFontManifest()
      .then((data) => {
        setFamilies(data);
        setSelectedIds(new Set(data.map((f) => f.id)));
      })
      .catch((err) => {
        console.error('Failed to load fonts manifest:', err);
      });
  }, []);

  const toggleSelect = (id: string) => {
    setSelectedIds((prev) => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  };

  const selectAll = () => {
    setSelectedIds(new Set(families.map((f) => f.id)));
  };

  const deselectAll = () => {
    setSelectedIds(new Set());
  };

  const handleInstallToSd = async () => {
    if (!canDirectWrite) return;
    const toInstall = families.filter((f) => selectedIds.has(f.id));
    if (toInstall.length === 0) return;

    setStatusMsg(null);
    setStatusType(null);
    setProgress(0);

    try {
      // Prompt user to pick SD card root
      const dirHandle = await (
        window as unknown as {
          showDirectoryPicker: (options?: { mode: 'readwrite' }) => Promise<FileSystemDirectoryHandle>;
        }
      ).showDirectoryPicker({ mode: 'readwrite' });

      setIsInstalling(true);
      await installFontsToDirectory(dirHandle, toInstall, (detail, pct) => {
        setProgress(pct);
        setStatusMsg(`${t('fonts_installing')} (${pct}%) ${detail}`);
        setStatusType('info');
      });

      setStatusMsg(t('fonts_install_done'));
      setStatusType('success');
      setProgress(100);
    } catch (err: unknown) {
      if ((err as Error)?.name === 'AbortError') {
        setStatusMsg(null);
        setStatusType(null);
      } else {
        console.error('SD write error:', err);
        setStatusMsg(t('fonts_install_err', { ERR: (err as Error)?.message || String(err) }));
        setStatusType('error');
      }
    } finally {
      setIsInstalling(false);
    }
  };

  const handleCustomUploadToSd = async () => {
    if (!canDirectWrite || !customFile) return;

    try {
      const dirHandle = await (
        window as unknown as {
          showDirectoryPicker: (options?: { mode: 'readwrite' }) => Promise<FileSystemDirectoryHandle>;
        }
      ).showDirectoryPicker({ mode: 'readwrite' });

      setIsCustomInstalling(true);
      const sub = customSubfolder.trim() || customFile.name.replace(/\.[^/.]+$/, '');
      await installCustomFontFile(dirHandle, customFile, sub);

      setStatusMsg(t('fonts_custom_done', { FILE: customFile.name }));
      setStatusType('success');
      setCustomFile(null);
      setCustomSubfolder('');
    } catch (err: unknown) {
      if ((err as Error)?.name === 'AbortError') return;
      console.error('Custom font SD write error:', err);
      setStatusMsg(t('fonts_install_err', { ERR: (err as Error)?.message || String(err) }));
      setStatusType('error');
    } finally {
      setIsCustomInstalling(false);
    }
  };

  return (
    <div className="bg-[#0b112c] border border-[#17234d] rounded-2xl p-6 space-y-6 shadow-xl">
      <div className="flex items-start justify-between gap-4">
        <div className="space-y-1">
          <div className="flex items-center gap-2.5">
            <div className="p-2 bg-blue-500/10 border border-blue-500/20 rounded-xl text-blue-400">
              <Type className="w-5 h-5" />
            </div>
            <h2 className="text-lg font-bold text-white tracking-wide">{t('fonts_section_title')}</h2>
          </div>
          <p className="text-xs text-[#8a9fc2] leading-relaxed max-w-2xl">{t('fonts_section_desc')}</p>
        </div>

        <div className="flex-shrink-0">
          {canDirectWrite ? (
            <span className="inline-flex items-center gap-1.5 px-3 py-1 rounded-full text-[11px] font-medium bg-emerald-500/10 border border-emerald-500/25 text-emerald-400">
              <HardDrive className="w-3.5 h-3.5" />
              {t('fonts_sd_badge_supported')}
            </span>
          ) : (
            <span className="inline-flex items-center gap-1.5 px-3 py-1 rounded-full text-[11px] font-medium bg-amber-500/10 border border-amber-500/25 text-amber-300">
              <Download className="w-3.5 h-3.5" />
              {t('fonts_sd_badge_unsupported')}
            </span>
          )}
        </div>
      </div>

      {/* Font list */}
      <div className="space-y-3">
        <div className="flex items-center justify-between text-xs text-[#8a9fc2] border-b border-[#17234d] pb-2">
          <span>
            {lang === 'ru'
              ? 'Доступные гарнитуры (в подпапках /fonts/<Шрифт>/):'
              : 'Available font families (in /fonts/<Font>/):'}
          </span>
          <div className="flex items-center gap-3">
            <button
              type="button"
              onClick={selectAll}
              disabled={isInstalling}
              className="text-blue-400 hover:text-blue-300 transition-colors cursor-pointer"
            >
              {t('fonts_select_all')}
            </button>
            <span>•</span>
            <button
              type="button"
              onClick={deselectAll}
              disabled={isInstalling}
              className="text-[#8a9fc2] hover:text-slate-300 transition-colors cursor-pointer"
            >
              {t('fonts_deselect_all')}
            </button>
          </div>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
          {families.map((fam) => {
            const isSelected = selectedIds.has(fam.id);
            const desc = lang === 'ru' ? fam.description : fam.descriptionEn;
            const sizeKb = Math.round(fam.bytes / 1024);

            return (
              <div
                key={fam.id}
                onClick={() => !isInstalling && toggleSelect(fam.id)}
                className={`p-3.5 rounded-xl border transition-all cursor-pointer select-none flex items-start gap-3 ${
                  isSelected
                    ? 'bg-blue-950/20 border-blue-500/40 text-white'
                    : 'bg-[#06091a]/50 border-[#17234d] text-slate-400 hover:border-[#223366]'
                }`}
              >
                <input
                  type="checkbox"
                  checked={isSelected}
                  disabled={isInstalling}
                  onChange={() => toggleSelect(fam.id)}
                  className="rounded bg-[#06091a] border-[#17234d] text-blue-500 focus:ring-blue-500/20 focus:ring-offset-0 mt-0.5"
                />
                <div className="space-y-1 flex-1 min-w-0">
                  <div className="flex items-center justify-between gap-2">
                    <span className="font-semibold text-xs tracking-wide">{fam.name}</span>
                    <span className="text-[10px] text-[#8a9fc2] font-mono">{sizeKb} KB</span>
                  </div>
                  <p className="text-[11px] text-[#8a9fc2] line-clamp-2">{desc}</p>
                  <div className="text-[10px] font-mono text-cyan-400/80 flex items-center gap-1 pt-0.5">
                    <Folder className="w-3 h-3" />
                    /fonts/{fam.dir}/
                  </div>
                </div>
              </div>
            );
          })}
        </div>
      </div>

      {/* Progress & Status */}
      {statusMsg && (
        <div
          className={`p-3 rounded-xl border text-xs flex items-start gap-2.5 ${
            statusType === 'success'
              ? 'bg-emerald-950/30 border-emerald-500/30 text-emerald-300'
              : statusType === 'error'
                ? 'bg-rose-950/30 border-rose-500/30 text-rose-300'
                : 'bg-blue-950/30 border-blue-500/30 text-blue-300'
          }`}
        >
          {statusType === 'success' ? (
            <CheckCircle2 className="w-4 h-4 flex-shrink-0 mt-0.5 text-emerald-400" />
          ) : statusType === 'error' ? (
            <AlertCircle className="w-4 h-4 flex-shrink-0 mt-0.5 text-rose-400" />
          ) : (
            <Loader2 className="w-4 h-4 flex-shrink-0 mt-0.5 animate-spin text-blue-400" />
          )}
          <span className="leading-relaxed flex-1">{statusMsg}</span>
        </div>
      )}

      {isInstalling && (
        <div className="w-full bg-[#06091a] rounded-full h-2 overflow-hidden border border-[#17234d]">
          <div className="bg-blue-500 h-full transition-all duration-300 ease-out" style={{ width: `${progress}%` }} />
        </div>
      )}

      {/* Action buttons */}
      <div className="flex flex-wrap items-center gap-3 pt-2">
        {canDirectWrite && (
          <button
            type="button"
            onClick={handleInstallToSd}
            disabled={isInstalling || selectedIds.size === 0}
            className="flex items-center gap-2 px-5 py-2.5 bg-blue-600 hover:bg-blue-500 disabled:bg-[#101736] disabled:text-[#8a9fc2] disabled:opacity-50 text-white font-semibold text-xs rounded-xl shadow-md transition-all active:scale-[0.98] cursor-pointer disabled:cursor-not-allowed"
          >
            {isInstalling ? <Loader2 className="w-4 h-4 animate-spin" /> : <HardDrive className="w-4 h-4" />}
            <span>{t('fonts_btn_install_sd')}</span>
          </button>
        )}

        <button
          type="button"
          onClick={downloadFontsZip}
          className="flex items-center gap-2 px-5 py-2.5 bg-[#0e1738] hover:bg-[#152354] border border-[#1d2d63] text-slate-200 hover:text-white font-semibold text-xs rounded-xl transition-all cursor-pointer"
        >
          <Download className="w-4 h-4 text-cyan-400" />
          <span>{t('fonts_btn_download_zip')}</span>
        </button>

        <button
          type="button"
          onClick={() => setShowCustom((prev) => !prev)}
          className="text-xs text-[#8a9fc2] hover:text-slate-300 transition-colors ml-auto cursor-pointer"
        >
          {showCustom ? '▲ ' : '▼ '}
          {t('fonts_custom_upload_title')}
        </button>
      </div>

      <p className="text-[11px] text-[#8a9fc2]/80 leading-relaxed italic">{t('fonts_zip_hint')}</p>

      {/* Custom font upload area */}
      {showCustom && (
        <div className="p-4 bg-[#06091a]/80 border border-[#17234d] rounded-xl space-y-4">
          <div className="text-xs font-semibold text-slate-300 flex items-center gap-2">
            <Upload className="w-4 h-4 text-blue-400" />
            {t('fonts_custom_upload_title')}
          </div>

          <div className="grid grid-cols-1 sm:grid-cols-2 gap-3 items-end">
            <div>
              <label className="block text-[11px] text-[#8a9fc2] mb-1">{t('fonts_custom_upload_hint')}</label>
              <input
                type="file"
                accept=".ttf,.otf"
                onChange={(e) => setCustomFile(e.target.files?.[0] || null)}
                className="w-full text-xs text-slate-300 file:mr-3 file:py-1.5 file:px-3 file:rounded-lg file:border-0 file:text-xs file:font-semibold file:bg-blue-600 file:text-white hover:file:bg-blue-500 cursor-pointer"
              />
            </div>

            <div>
              <label className="block text-[11px] text-[#8a9fc2] mb-1">{t('fonts_custom_subfolder_label')}</label>
              <input
                type="text"
                placeholder="MyCustomFont"
                value={customSubfolder}
                onChange={(e) => setCustomSubfolder(e.target.value)}
                className="w-full bg-[#0b112c] border border-[#17234d] rounded-lg px-3 py-1.5 text-xs text-white placeholder-slate-500 focus:outline-none focus:border-blue-500"
              />
            </div>
          </div>

          {canDirectWrite && customFile && (
            <div className="flex justify-end pt-1">
              <button
                type="button"
                onClick={handleCustomUploadToSd}
                disabled={isCustomInstalling}
                className="flex items-center gap-2 px-4 py-2 bg-emerald-600 hover:bg-emerald-500 text-white font-semibold text-xs rounded-lg transition-all cursor-pointer disabled:opacity-50"
              >
                {isCustomInstalling ? (
                  <Loader2 className="w-3.5 h-3.5 animate-spin" />
                ) : (
                  <HardDrive className="w-3.5 h-3.5" />
                )}
                <span>{t('fonts_custom_btn_upload')}</span>
              </button>
            </div>
          )}
        </div>
      )}
    </div>
  );
};
