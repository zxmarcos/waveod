import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

// The build goes into the Python package, which serves it in the pywebview window (waveod_studio/webapp.py).
export default defineConfig({
  plugins: [svelte()],
  base: './',
  build: {
    outDir: '../waveod_studio/web',
    emptyOutDir: true,
    target: 'es2022',
  },
  server: { port: 5173, strictPort: true },
});
