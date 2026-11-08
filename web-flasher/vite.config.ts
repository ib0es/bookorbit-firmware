import { defineConfig, Plugin } from 'vite';
import react from '@vitejs/plugin-react';
import fs from 'fs';
import path from 'path';

/**
 * The app is built into ../docs next to the firmware binaries (builds/, manifests/),
 * so the outDir cannot be emptied. Remove only the previous hashed bundles instead.
 */
function cleanAssets(): Plugin {
  let assetsDir = '';
  return {
    name: 'clean-old-assets',
    apply: 'build',
    configResolved(cfg) {
      assetsDir = path.resolve(cfg.root, cfg.build.outDir, cfg.build.assetsDir);
    },
    buildStart() {
      fs.rmSync(assetsDir, { recursive: true, force: true });
    },
  };
}

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [react(), cleanAssets()],
  base: './', // relative assets for GitHub Pages and local viewing
  build: {
    outDir: '../docs',
    emptyOutDir: false, // keep firmware binary folders (builds/, manifests/) intact
  },
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
    },
  },
});
