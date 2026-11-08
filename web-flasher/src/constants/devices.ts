import { DeviceInfo, DeviceKey } from '../types';

export const DEVICES: Record<DeviceKey, DeviceInfo> = {
  x4: {
    name: 'Xteink X4',
    chip: 'ESP32-C3',
    chipSubtitle: 'ESP32-C3',
    desc_en: 'Xteink X4: 6" 800x600 E-Ink, ESP32-C3, 5 physical buttons',
    desc_ru: 'Xteink X4: экран E-Ink 6" 800x600, чип ESP32-C3, 5 кнопок',
    merged: 'builds/x4/bookorbit-x4.bin'
  },
  x3: {
    name: 'Xteink X3',
    chip: 'ESP32-C3',
    chipSubtitle: 'ESP32-C3',
    desc_en: 'Xteink X3: 792x528 UC8253/UC8279, ESP32-C3, 5 physical buttons',
    desc_ru: 'Xteink X3: экран 792x528 UC8253/UC8279, чип ESP32-C3, 5 кнопок',
    merged: 'builds/x3/bookorbit-x3.bin'
  },
  x4c: {
    name: 'X4 Classic',
    chip: 'ESP32-S3',
    chipSubtitle: 'ESP32-S3',
    desc_en: 'Xteink X4 Classic: ESP32-S3 (8MB PSRAM), 8 physical buttons',
    desc_ru: 'Xteink X4 Classic: ESP32-S3 (8MB PSRAM), 8 кнопок, без тачскрина',
    merged: 'builds/x4c/bookorbit-x4c.bin'
  },
  x4pro: {
    name: 'X4 Pro',
    chip: 'ESP32-S3',
    chipSubtitle: 'ESP32-S3 Touch',
    desc_en: 'Xteink X4 Pro: ESP32-S3 (8MB PSRAM), GT911 touch screen + frontlight',
    desc_ru: 'Xteink X4 Pro: ESP32-S3 (8MB PSRAM), сенсорный экран GT911 + подсветка',
    merged: 'builds/x4pro/bookorbit-x4pro.bin'
  },
  picco: {
    name: 'BOOX Picco',
    chip: 'ESP32-S3',
    chipSubtitle: 'ESP32-S3 Touch',
    desc_en: 'BOOX Picco / Tiles: ESP32-S3 (8MB PSRAM), touch screen + I2C frontlight',
    desc_ru: 'BOOX Picco / Tiles: ESP32-S3 (8MB PSRAM), сенсорный экран + подсветка',
    merged: 'builds/picco/bookorbit-picco.bin'
  }
};
