import { FontFamilyMeta } from '../types';

/**
 * Checks whether the File System Access API (showDirectoryPicker) is supported.
 */
export function isFileSystemAccessSupported(): boolean {
  return (
    typeof window !== 'undefined' &&
    typeof (window as unknown as { showDirectoryPicker?: unknown }).showDirectoryPicker === 'function'
  );
}

/**
 * Loads font metadata from /fonts/fonts.json.
 */
export async function fetchFontManifest(): Promise<FontFamilyMeta[]> {
  const resp = await fetch(`${import.meta.env.BASE_URL}fonts/fonts.json`);
  if (!resp.ok) {
    throw new Error(`Failed to load font manifest: HTTP ${resp.status}`);
  }
  return (await resp.json()) as FontFamilyMeta[];
}

/**
 * Installs selected font families to the SD card directory under /fonts/<Family>/.
 */
export async function installFontsToDirectory(
  dirHandle: FileSystemDirectoryHandle,
  families: FontFamilyMeta[],
  onProgress?: (msg: string, percent: number) => void,
): Promise<void> {
  const fontsDir = await dirHandle.getDirectoryHandle('fonts', { create: true });

  const totalFiles = families.reduce((sum, f) => sum + f.files.length, 0);
  let writtenFiles = 0;

  for (const family of families) {
    const familyDir = await fontsDir.getDirectoryHandle(family.dir, { create: true });

    for (const fileName of family.files) {
      if (onProgress) {
        const pct = Math.floor((writtenFiles / (totalFiles || 1)) * 100);
        onProgress(`${family.name} / ${fileName}`, pct);
      }

      const fileUrl = `${import.meta.env.BASE_URL}fonts/${family.dir}/${fileName}`;
      const resp = await fetch(fileUrl);
      if (!resp.ok) {
        throw new Error(`Failed to fetch ${fileUrl}: HTTP ${resp.status}`);
      }
      const data = await resp.arrayBuffer();

      const fileHandle = await familyDir.getFileHandle(fileName, { create: true });
      const writable = await fileHandle.createWritable();
      await writable.write(data);
      await writable.close();

      writtenFiles++;
    }
  }

  if (onProgress) {
    onProgress('Done', 100);
  }
}

/**
 * Installs a custom font file onto the SD card into /fonts/<subfolder>/ (or /fonts/).
 */
export async function installCustomFontFile(
  dirHandle: FileSystemDirectoryHandle,
  file: File,
  subfolder?: string,
): Promise<void> {
  const fontsDir = await dirHandle.getDirectoryHandle('fonts', { create: true });
  let targetDir = fontsDir;
  if (subfolder && subfolder.trim()) {
    targetDir = await fontsDir.getDirectoryHandle(subfolder.trim(), { create: true });
  }

  const fileHandle = await targetDir.getFileHandle(file.name, { create: true });
  const writable = await fileHandle.createWritable();
  await writable.write(await file.arrayBuffer());
  await writable.close();
}

/**
 * Triggers browser download of /fonts.zip.
 */
export function downloadFontsZip(): void {
  const link = document.createElement('a');
  link.href = `${import.meta.env.BASE_URL}fonts.zip`;
  link.download = 'fonts.zip';
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
}

export const fontFamilyKey = (name: string): string => name.toLowerCase().replace(/[ _-]/g, '');
export const fontFamilyFromFile = (name: string): string =>
  name
    .replace(/\.(ttf|otf)$/i, '')
    .replace(/[- ](regular|book|roman|normal|bolditalic|boldoblique|bold|italic|oblique|bi|it)$/i, '');
