// Vite-Konfiguration: baut die UI als einzelnes IIFE-Bundle (app.js/app.css) und proxyt API-Aufrufe im Dev-Modus an die Lampe.
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { resolve } from 'path';
import { defineConfig } from 'vite';

export default defineConfig({
  plugins: [
    svelte()
  ],
  build: {
    outDir: 'dist',
    assetsDir: '',
    rollupOptions: {
      input: resolve(import.meta.dirname, 'src/main.js'),
      output: {
        format: 'iife',
        name: 'App',
        entryFileNames: 'app.js',
        assetFileNames: (assetInfo) => {
          if (assetInfo.name?.endsWith('.css')) {
            return 'app.css';
          }
          return assetInfo.name || '';
        }
      }
    }
  },
  server: {
    // Nur API-Aufrufe und Lampen-Bild gehen an die Lampe (AP-IP 4.3.2.1). Statische Dateien wie
    // pico.min.css bewusst NICHT proxyen: ist die Lampe nicht erreichbar, würde das Stylesheet hängen
    // und die ganze Seite blockieren (auch im Mock-Modus).
    proxy: {
      '^/(get_|set_|trigger_|test_wifi$|ping$|restart$|factory_reset$|meerkat\\.avif$)': {
        target: 'http://4.3.2.1',
        changeOrigin: true
      }
    }
  }
});
