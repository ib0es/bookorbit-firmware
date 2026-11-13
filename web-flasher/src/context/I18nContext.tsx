import React, { useCallback, useMemo, useState } from 'react';
import { Language } from '../types';
import { I18nContext } from './i18n';
import en from '../locales/en.json';
import ru from '../locales/ru.json';

type Dict = Record<string, string>;
const DICTS: Record<Language, Dict> = { en, ru };
const STORAGE_KEY = 'bookorbit-flasher-lang';

function initialLang(): Language {
  try {
    const saved = localStorage.getItem(STORAGE_KEY);
    if (saved === 'en' || saved === 'ru') return saved;
  } catch {
    /* storage unavailable */
  }
  return typeof navigator !== 'undefined' && navigator.language?.toLowerCase().startsWith('ru') ? 'ru' : 'en';
}

export const I18nProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const [lang, setLangState] = useState<Language>(initialLang);

  const setLang = useCallback((l: Language) => {
    setLangState(l);
    try {
      localStorage.setItem(STORAGE_KEY, l);
    } catch {
      /* storage unavailable */
    }
  }, []);

  const t = useCallback(
    (key: string, params: Record<string, string | number> = {}) => {
      let text = DICTS[lang][key] ?? DICTS.en[key] ?? key;
      for (const [k, v] of Object.entries(params)) text = text.split(`%${k}%`).join(String(v));
      return text;
    },
    [lang],
  );

  const value = useMemo(() => ({ lang, setLang, t }), [lang, setLang, t]);
  return <I18nContext.Provider value={value}>{children}</I18nContext.Provider>;
};
