#!/usr/bin/env node

import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import { promisify } from "util";
import { gzip } from "zlib";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const gzipAsync = promisify(gzip);

const DIST_DIR = path.join(__dirname, "dist");
const DATA_DIR = path.join(__dirname, "..", "data");

async function compressFile(inputPath, outputPath) {
  try {
    const data = await fs.promises.readFile(inputPath);
    const compressed = await gzipAsync(data);
    await fs.promises.writeFile(outputPath, compressed);

    const originalSize = data.length;
    const compressedSize = compressed.length;
    const savings = (
      ((originalSize - compressedSize) / originalSize) *
      100
    ).toFixed(1);

    console.log(
      `✓ ${path.basename(inputPath)} -> ${path.basename(
        outputPath
      )} (${originalSize}B -> ${compressedSize}B, ${savings}% savings)`
    );
  } catch (error) {
    console.error(`✗ Failed to compress ${inputPath}:`, error.message);
  }
}

async function processFiles() {
  console.log("🔨 Processing Svelte build for ESP32...\n");

  // Ensure data directory exists
  if (!fs.existsSync(DATA_DIR)) {
    fs.mkdirSync(DATA_DIR, { recursive: true });
  }

  // Check if dist directory exists
  if (!fs.existsSync(DIST_DIR)) {
    console.error('❌ Dist directory not found. Run "npm run build" first.');
    process.exit(1);
  }

  const filesToProcess = [
    { src: "index.html", dest: "index.html.gzip" },
    { src: "app.js", dest: "app.js.gzip" },
    { src: "app.css", dest: "app.css.gzip" },
  ];

  let processedCount = 0;

  for (const file of filesToProcess) {
    const srcPath = path.join(DIST_DIR, file.src);
    const destPath = path.join(DATA_DIR, file.dest);

    if (fs.existsSync(srcPath)) {
      await compressFile(srcPath, destPath);
      processedCount++;
    } else {
      console.log(`⚠️  ${file.src} not found in dist, skipping...`);
    }
  }

  // Also copy pico.min.css as compressed if it exists
  const picoSrc = path.join(DATA_DIR, "pico.min.css");
  const picoDest = path.join(DATA_DIR, "pico.min.css.gzip");

  if (fs.existsSync(picoSrc)) {
    await compressFile(picoSrc, picoDest);
    processedCount++;
  }

  console.log(`\n✅ Processed ${processedCount} files successfully!`);
  console.log(
    "📁 Files are ready in the data/ directory for ESP32 deployment."
  );

  // List all .gz files in data directory
  console.log("\n📋 Compressed files in data/:");
  const dataFiles = fs.readdirSync(DATA_DIR).filter((f) => f.endsWith(".gz"));
  dataFiles.forEach((file) => {
    const size = fs.statSync(path.join(DATA_DIR, file)).size;
    console.log(`   ${file} (${size} bytes)`);
  });
}

// Run the script
processFiles().catch((error) => {
  console.error("❌ Build script failed:", error);
  process.exit(1);
});
