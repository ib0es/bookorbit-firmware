export type Language = 'en' | 'ru';

export type DeviceKey = 'x4' | 'x3' | 'x4c' | 'x4pro' | 'picco';

export interface DeviceInfo {
  usbMassStorage: boolean;
  name: string;
  chip: 'ESP32-C3' | 'ESP32-S3';
  chipSubtitle: string;
  desc_en: string;
  desc_ru: string;
  merged: string;
  app?: string;
}

export interface GitHubReleaseAsset {
  name: string;
  browser_download_url: string;
  size: number;
}

export interface GitHubRelease {
  tag_name: string;
  name?: string;
  published_at?: string;
  prerelease?: boolean;
  assets?: GitHubReleaseAsset[];
}

export type ConnectionStatus = 'disconnected' | 'connecting' | 'connected' | 'flashing' | 'error';

/** What the flasher is doing right now while status === 'flashing'. */
export type FlashPhase = 'idle' | 'preparing' | 'erasing' | 'writing' | 'restarting';

export interface InstalledFont {
  capacityBytes?: number;
  families?: Array<{ name: string; bytes: number; offset: number; span: number }>;
  freeBytes?: number;
  usedBytes?: number;
  largestFreeBytes?: number;
  status: 'installed' | 'empty' | 'unavailable' | 'unknown';
  name?: string;
}

export interface DetectionResult {
  inspectionError?: string;
  supportsFontCatalog?: boolean;
  installedFont?: InstalledFont;
  /** Model confirmed by hardware probe and/or installed Book Orbit build. */
  exactMatchKey: DeviceKey | null;
  exactMatchName: string | null;
  chipFamily: string;
  mac: string;
  isBookOrbitInstalled?: boolean;
  /** Hardware probe and installed firmware disagree about the model. */
  modelConflict?: boolean;
}

export const EMPTY_DETECTION: DetectionResult = {
  exactMatchKey: null,
  exactMatchName: null,
  chipFamily: '',
  mac: '',
};

export interface DeviceInitialConfig {
  serverUrl?: string;
  username?: string;
  password?: string;
  wifiSsid?: string;
  wifiPass?: string;
}

/** 'clean': erase whole flash; 'update': keep the NVS partition (settings, token). */
export type FlashMode = 'clean' | 'update';

export interface FontFamilyMeta {
  id: string;
  name: string;
  description: string;
  descriptionEn: string;
  dir: string;
  files: string[];
  bytes: number;
}
