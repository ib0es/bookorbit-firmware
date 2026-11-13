import type { InstalledFont } from '../types';
import type { FirmwareSignature } from './firmwareImage';

/** Retry a read-only inspection by opening a fresh stub session after failure.
 * Never retry a flash write: a failed stream may be desynchronised.
 */
export async function inspectReaderSession<Session, Result>(
  open: () => Promise<Session>,
  inspect: (session: Session) => Promise<Result>,
  close: () => Promise<void>,
  onRetry: (error: unknown) => void,
): Promise<{ session: Session; result: Result }> {
  for (let attempt = 0; attempt < 2; attempt++) {
    try {
      const session = await open();
      return { session, result: await inspect(session) };
    } catch (error) {
      await close();
      if (attempt === 1) throw error;
      onRetry(error);
    }
  }
  throw new Error('Reader inspection failed.');
}

/** Optional metadata must not turn a successfully opened USB connection into a failed one. */
export async function inspectFlashState(
  readFirmware: () => Promise<FirmwareSignature>,
  readFont: () => Promise<InstalledFont>,
  allowIncomplete: boolean,
): Promise<{
  signature: FirmwareSignature;
  installedFont: InstalledFont;
  firmwareInspected: boolean;
  inspectionError?: string;
}> {
  let signature: FirmwareSignature = { isBookOrbit: false, board: null };
  let installedFont: InstalledFont = { status: 'unknown' };
  let firmwareInspected = false;
  try {
    signature = await readFirmware();
    firmwareInspected = true;
    if (signature.isBookOrbit) installedFont = await readFont();
    return { signature, installedFont, firmwareInspected };
  } catch (error) {
    if (!allowIncomplete) throw error;
    return {
      signature,
      installedFont,
      firmwareInspected,
      inspectionError: error instanceof Error ? error.message : String(error),
    };
  }
}
