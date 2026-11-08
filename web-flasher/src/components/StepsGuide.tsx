import React from 'react';
import { useI18n } from '../context/I18nContext';

export const StepsGuide: React.FC = () => {
  const { t } = useI18n();

  const steps = [
    { num: 1, title: t('step1_h'), desc: t('step1_d') },
    { num: 2, title: t('step2_h'), desc: t('step2_d') },
    { num: 3, title: t('step3_h'), desc: t('step3_d') },
    { num: 4, title: t('step4_h'), desc: t('step4_d') },
  ];

  return (
    <div className="bg-[#0b112c] border border-[#17234d] rounded-2xl p-6 shadow-md">
      <h3 className="text-xs font-bold uppercase tracking-wider text-[#8a9fc2] mb-4">
        {t('steps_title')}
      </h3>

      <div className="grid grid-cols-1 gap-4">
        {steps.map((s) => (
          <div key={s.num} className="flex items-start gap-3">
            <div className="flex-shrink-0 w-6 h-6 rounded-full bg-blue-500/10 border border-blue-500/30 text-cyan-400 font-bold text-xs flex items-center justify-center">
              {s.num}
            </div>
            <div>
              <div className="text-sm font-semibold text-white">{s.title}</div>
              <div className="text-xs text-[#8a9fc2] mt-0.5 leading-relaxed">{s.desc}</div>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
};
