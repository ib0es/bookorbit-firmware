import { encodeNVS, NVSNamespaces, NVSValue } from '@m1kad0/esp-nvs-utils';
import { DeviceInitialConfig } from '../types';
import { FlasherError } from './firmwareImage';

/** Preferences namespace used by the firmware (src/hal/storage.cpp). */
export const NVS_NAMESPACE = 'bookorbit';

const utf8Len = (s: string) => new TextEncoder().encode(s).length;

/**
 * Validates the optional setup form and turns it into NVS string entries.
 * Passwords and SSID are kept verbatim: leading/trailing spaces are legal there.
 */
export function buildConfigEntries(cfg: DeviceInitialConfig): NVSValue[] {
  const serverUrl = cfg.serverUrl?.trim() ?? '';
  const username = cfg.username?.trim() ?? '';
  const password = cfg.password ?? '';
  const wifiSsid = cfg.wifiSsid ?? '';
  const wifiPass = cfg.wifiPass ?? '';

  if (wifiPass && !wifiSsid.trim()) throw new FlasherError('err_wifi_ssid_missing');
  if (wifiSsid && utf8Len(wifiSsid) > 32) throw new FlasherError('err_wifi_ssid_len');
  if (wifiPass && (wifiPass.length < 8 || wifiPass.length > 63)) throw new FlasherError('err_wifi_pass_len');
  if (serverUrl && !/^https?:\/\/[^\s/]+/i.test(serverUrl)) throw new FlasherError('err_server_url');

  const entries: NVSValue[] = [];
  const add = (name: string, value: string) => {
    if (value) entries.push({ name, encoding: 'string', value });
  };
  add('server_url', serverUrl);
  add('username', username);
  add('password', password);
  add('wifi_ssid', wifiSsid.trim() ? wifiSsid : '');
  add('wifi_pass', wifiPass);
  return entries;
}

/**
 * Builds a full NVS partition image: `existing` namespaces with `entries`
 * merged into the bookorbit namespace, padded with 0xFF to `partitionSize`
 * so no stale pages survive.
 */
export function buildNvsPartition(existing: NVSNamespaces, entries: NVSValue[], partitionSize: number): Uint8Array {
  const namespaces: NVSNamespaces = { ...existing };
  const kept = (namespaces[NVS_NAMESPACE] ?? []).filter((e) => !entries.some((n) => n.name === e.name));
  namespaces[NVS_NAMESPACE] = [...kept, ...entries];

  const encoded = encodeNVS(namespaces);
  if (encoded.length > partitionSize) {
    throw new FlasherError('err_nvs_too_big');
  }
  const out = new Uint8Array(partitionSize).fill(0xff);
  out.set(encoded);
  return out;
}
