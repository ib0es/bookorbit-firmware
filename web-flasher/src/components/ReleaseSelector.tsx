import React, { useState } from 'react';
import { useI18n } from '../context/i18n';
import { GitHubRelease } from '../types';
import { Layers, Upload, FileCode } from 'lucide-react';

interface ReleaseSelectorProps {
  selectedRelease: string;
  onSelectRelease: (rel: string) => void;
  releases: GitHubRelease[];
  customFileName: string;
  customFileSizeMb: string;
  onFileSelect: (file: File) => void;
  disabled?: boolean;
}

export const ReleaseSelector: React.FC<ReleaseSelectorProps> = ({
  selectedRelease,
  onSelectRelease,
  releases,
  customFileName,
  customFileSizeMb,
  onFileSelect,
  disabled = false,
}) => {
  const { t } = useI18n();
  const [isDragging, setIsDragging] = useState(false);
  const isCustom = selectedRelease === 'custom';

  const handleDragOver = (e: React.DragEvent) => {
    e.preventDefault();
    e.stopPropagation();
    setIsDragging(true);
  };

  const handleDragLeave = (e: React.DragEvent) => {
    e.preventDefault();
    e.stopPropagation();
    setIsDragging(false);
  };

  const handleDrop = (e: React.DragEvent) => {
    e.preventDefault();
    e.stopPropagation();
    setIsDragging(false);

    const files = e.dataTransfer.files;
    if (!disabled && files && files.length > 0) {
      onFileSelect(files[0]);
    }
  };

  const handleInputChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const files = e.target.files;
    if (files && files.length > 0) {
      onFileSelect(files[0]);
    }
  };

  return (
    <div
      onDragOver={handleDragOver}
      onDragLeave={handleDragLeave}
      onDrop={handleDrop}
      className={`w-full max-w-md bg-[#06091a] border rounded-xl p-3.5 text-left transition-all ${
        isDragging
          ? 'border-blue-400 bg-blue-950/30 shadow-lg shadow-blue-950/40 ring-2 ring-blue-500/30'
          : 'border-[#17234d]'
      }`}
    >
      <div className="flex flex-col gap-1.5">
        <label
          htmlFor="release-select"
          className="flex items-center gap-1.5 text-[11px] font-bold uppercase tracking-wider text-[#8a9fc2]"
        >
          <Layers className="w-3.5 h-3.5 text-cyan-400" />
          <span>{t('release_label')}</span>
        </label>

        <select
          id="release-select"
          value={selectedRelease}
          disabled={disabled}
          onChange={(e) => onSelectRelease(e.target.value)}
          className="w-full bg-[#0b112c] border border-[#17234d] hover:border-[#23366f] text-white text-xs font-medium rounded-lg px-3 py-2 outline-none cursor-pointer transition-colors"
        >
          <option value="latest">{t('release_latest')}</option>

          {releases.map((rel) => {
            const tag = rel.tag_name;
            const dateStr = rel.published_at ? ` (${new Date(rel.published_at).toLocaleDateString()})` : '';
            return (
              <option key={tag} value={`tag:${tag}`}>
                {tag}
                {dateStr}
                {rel.prerelease ? ' [Pre-release]' : ''}
              </option>
            );
          })}

          <option value="custom">{t('release_custom')}</option>
        </select>
      </div>

      {isCustom && (
        <div className="mt-3 pt-3 border-t border-dashed border-[#17234d] space-y-2">
          <label
            onDragOver={handleDragOver}
            onDragLeave={handleDragLeave}
            onDrop={handleDrop}
            className={`flex flex-col items-center justify-center gap-2 p-4 border-2 border-dashed rounded-xl cursor-pointer transition-all select-none ${
              isDragging
                ? 'border-blue-400 bg-blue-500/15 text-blue-300 scale-[1.01]'
                : 'border-[#17234d] hover:border-blue-500/50 bg-[#0b112c] text-[#8a9fc2] hover:text-slate-200'
            }`}
          >
            <div
              className={`p-2 rounded-lg transition-colors ${
                isDragging ? 'bg-blue-500/20 text-cyan-400' : 'bg-[#06091a] text-[#8a9fc2]'
              }`}
            >
              <Upload className="w-5 h-5" />
            </div>

            <div className="text-center">
              <span className="text-xs font-medium block">
                {isDragging ? t('custom_file_drop_active') : customFileName ? customFileName : t('custom_file_prompt')}
              </span>
              <span className="text-[10px] text-[#8a9fc2] block mt-0.5 font-mono">.bin firmware</span>
            </div>

            <input type="file" accept=".bin" disabled={disabled} onChange={handleInputChange} className="hidden" />
          </label>

          {customFileName && (
            <div className="flex items-center gap-1.5 text-xs text-cyan-400 font-medium">
              <FileCode className="w-3.5 h-3.5 flex-shrink-0" />
              <span className="break-all">
                {t('custom_file_ready', { NAME: customFileName, SIZE: customFileSizeMb })}
              </span>
            </div>
          )}
        </div>
      )}
    </div>
  );
};
