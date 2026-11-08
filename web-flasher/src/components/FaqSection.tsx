import React from 'react';
import { useI18n } from '../context/I18nContext';
import { HelpCircle } from 'lucide-react';

export const FaqSection: React.FC = () => {
  const { t } = useI18n();

  const faqs = [
    { q: t('faq1_q'), a: t('faq1_a') },
    { q: t('faq2_q'), a: t('faq2_a') },
    { q: t('faq3_q'), a: t('faq3_a') }
  ];

  return (
    <div className="bg-[#0b112c] border border-[#17234d] rounded-2xl p-6 shadow-md">
      <div className="flex items-center gap-2 mb-4">
        <HelpCircle className="w-4 h-4 text-cyan-400" />
        <h3 className="text-xs font-bold uppercase tracking-wider text-[#8a9fc2]">
          {t('faq_title')}
        </h3>
      </div>

      <div className="space-y-3">
        {faqs.map((faq, i) => (
          <details
            key={i}
            className="group bg-[#06091a] border border-[#17234d] rounded-xl overflow-hidden text-left"
          >
            <summary className="p-3.5 text-xs font-semibold text-white cursor-pointer select-none hover:text-cyan-400 transition-colors flex items-center justify-between">
              <span>{faq.q}</span>
              <span className="text-[#8a9fc2] group-open:rotate-180 transition-transform">▼</span>
            </summary>
            <div className="px-3.5 pb-3.5 text-xs text-[#8a9fc2] leading-relaxed border-t border-[#17234d]/60 pt-2.5">
              {faq.a}
            </div>
          </details>
        ))}
      </div>
    </div>
  );
};
