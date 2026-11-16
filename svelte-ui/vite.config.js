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
      input: resolve(__dirname, 'src/main.js'),
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
    proxy: {
      '/get_config': 'http://192.168.4.1',
      '/set_config': 'http://192.168.4.1',
      '/get_system_config': 'http://192.168.4.1',
      '/set_system_config': 'http://192.168.4.1'
    }
  }
});