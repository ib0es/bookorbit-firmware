import React, { useState, useEffect, useRef } from 'react';
import { Terminal, Trash2, ChevronDown } from 'lucide-react';

interface FlashConsoleProps {
  logs: string[];
  onClear: () => void;
}

export const FlashConsole: React.FC<FlashConsoleProps> = ({ logs, onClear }) => {
  const [isExpanded, setIsExpanded] = useState(false);
  const containerRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (isExpanded && containerRef.current) {
      containerRef.current.scrollTop = containerRef.current.scrollHeight;
    }
  }, [logs, isExpanded]);

  if (logs.length === 0) return null;

  return (
    <div className="w-full bg-[#06091a] border border-[#17234d] rounded-xl overflow-hidden text-left shadow-inner transition-all">
      <div className="flex items-center justify-between px-3.5 py-2.5 bg-[#0b112c] border-b border-[#17234d]/60 text-[11px] font-mono text-[#8a9fc2]">
        <button
          type="button"
          onClick={() => setIsExpanded((prev) => !prev)}
          className="flex items-center gap-2 hover:text-white transition-colors cursor-pointer select-none"
        >
          <Terminal className="w-3.5 h-3.5 text-cyan-400" />
          <span className="font-semibold text-slate-200">Console Log</span>
          <span className="text-[10px] bg-blue-500/10 border border-blue-500/30 px-1.5 py-0.5 rounded text-cyan-400">
            {logs.length} lines
          </span>
          <ChevronDown
            className={`w-3.5 h-3.5 text-[#8a9fc2] transition-transform duration-200 ${
              isExpanded ? 'rotate-180 text-white' : ''
            }`}
          />
        </button>

        <div className="flex items-center gap-2">
          {isExpanded && (
            <button
              type="button"
              onClick={onClear}
              title="Clear logs"
              className="hover:text-white transition-colors p-1"
            >
              <Trash2 className="w-3.5 h-3.5" />
            </button>
          )}
        </div>
      </div>

      {isExpanded && (
        <div
          ref={containerRef}
          className="p-3.5 font-mono text-xs text-slate-300 max-h-56 overflow-y-auto custom-scrollbar space-y-1 whitespace-pre-wrap select-text border-t border-[#17234d]/40"
        >
          {logs.map((line, idx) => (
            <div key={idx} className="leading-relaxed">
              {line}
            </div>
          ))}
        </div>
      )}
    </div>
  );
};
