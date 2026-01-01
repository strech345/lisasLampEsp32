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
      input: {
        main: resolve(__dirname, 'index.html')
      },
      output: {
        entryFileNames: 'app.js',
        chunkFileNames: 'app.js',
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
    proxy: {
      '/get_config': 'http://192.168.4.1',
      '/set_config': 'http://192.168.4.1',
      '/get_system_config': 'http://192.168.4.1',
      '/set_system_config': 'http://192.168.4.1'
    }
  }
});