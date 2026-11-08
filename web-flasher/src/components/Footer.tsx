import React from 'react';
import { Github } from 'lucide-react';

export const Footer: React.FC = () => {
  return (
    <footer className="pt-6 pb-12 text-center text-xs text-[#8a9fc2] border-t border-[#17234d] flex flex-col sm:flex-row items-center justify-center gap-3">
      <span>Book Orbit Firmware · Powered by esptool-js & Web Serial API</span>
      <span className="hidden sm:inline">·</span>
      <a
        href="https://github.com/igor-sp/book_orbit"
        target="_blank"
        rel="noopener noreferrer"
        className="inline-flex items-center gap-1.5 text-slate-300 hover:text-white transition-colors"
      >
        <Github className="w-3.5 h-3.5" />
        <span>GitHub</span>
      </a>
    </footer>
  );
};
