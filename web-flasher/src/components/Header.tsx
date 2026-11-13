import React from 'react';
import { useI18n } from '../context/i18n';
import { BookOpen, ExternalLink } from 'lucide-react';

export const Header: React.FC = () => {
  const { lang, setLang, t } = useI18n();

  return (
    <header className="flex flex-col sm:flex-row items-center justify-between pb-6 border-b border-[#17234d] gap-4">
      <div className="flex items-center gap-3.5">
        <div className="p-2.5 bg-blue-500/10 border border-blue-500/30 rounded-xl text-cyan-400 shadow-[0_0_15px_rgba(34,211,238,0.2)] flex-shrink-0">
          <BookOpen className="w-6 h-6 text-cyan-400" />
        </div>
        <div>
          <div className="flex items-center gap-2.5">
            <h1 className="text-2xl font-extrabold tracking-tight text-white">Book Orbit</h1>
            <span className="text-[10px] font-bold uppercase tracking-wider text-cyan-400 bg-cyan-500/10 border border-cyan-500/20 px-2 py-0.5 rounded-full">
              Web Flasher
            </span>
          </div>
          <p className="text-xs text-[#8a9fc2] mt-0.5">{t('subtitle')}</p>
        </div>
      </div>

      <div className="flex items-center gap-3">
        <a
          href="https://bookorbit.app"
          target="_blank"
          rel="noopener noreferrer"
          className="flex items-center gap-1.5 text-xs font-medium text-[#8a9fc2] hover:text-white transition-colors px-3 py-1.5 rounded-lg border border-[#17234d] hover:border-[#23366f] bg-[#0b112c]/60"
        >
          <span>bookorbit.app</span>
          <ExternalLink className="w-3.5 h-3.5" />
        </a>

        {/* Language Switcher */}
        <div className="flex items-center bg-[#0b112c] border border-[#17234d] rounded-lg p-0.5">
          <button
            onClick={() => setLang('en')}
            className={`px-3 py-1 text-xs font-semibold rounded-md transition-colors ${
              lang === 'en' ? 'bg-blue-600 text-white shadow-sm' : 'text-[#8a9fc2] hover:text-white'
            }`}
          >
            EN
          </button>
          <button
            onClick={() => setLang('ru')}
            className={`px-3 py-1 text-xs font-semibold rounded-md transition-colors ${
              lang === 'ru' ? 'bg-blue-600 text-white shadow-sm' : 'text-[#8a9fc2] hover:text-white'
            }`}
          >
            RU
          </button>
        </div>
      </div>
    </header>
  );
};
