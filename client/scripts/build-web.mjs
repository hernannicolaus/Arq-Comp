
import { readFile, writeFile, mkdir } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { minify } from "html-minifier-terser";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const clientDir = path.resolve(scriptDir, "..");
const srcDir = path.join(clientDir, "src");

const htmlPath = path.join(srcDir, "index.html");
const cssPath = path.join(srcDir, "style.css");

// sketch_oct19a está al mismo nivel que client.
const outputPath = path.resolve(
  clientDir,
  "..",
  "sketch_oct19a",
  "web_page.h"
);

let html = await readFile(htmlPath, "utf8");
const css = await readFile(cssPath, "utf8");

// Incorporar el CSS al HTML.
const inlineStyle = `<style>\n${css}\n</style>`;
const stylesheetLink =
  /<link\b(?=[^>]*\brel\s*=\s*["']?stylesheet["']?)[^>]*>/i;

if (stylesheetLink.test(html)) {
  html = html.replace(stylesheetLink, inlineStyle);
} else if (/<\/head\s*>/i.test(html)) {
  html = html.replace(/<\/head\s*>/i, `${inlineStyle}\n</head>`);
} else {
  throw new Error(
    "No se encontró dónde insertar el CSS. Revisá index.html."
  );
}

const minifiedHtml = await minify(html, {
  collapseWhitespace: true,
  removeComments: true,
  minifyCSS: true,
  minifyJS: true,
});

if (!minifiedHtml.trim()) {
  throw new Error("El HTML generado está vacío.");
}

// Evitar conflictos con el delimitador de C++ raw strings.
let delimiter = "WEBHTML";
let suffix = 0;

while (minifiedHtml.includes(`)${delimiter}"`)) {
  delimiter = `WEBHTM${++suffix}`;
}

const header = `#pragma once
#include <pgmspace.h>

// Generado automáticamente. No editar manualmente.
const char WEB_PAGE[] PROGMEM = R"${delimiter}(${minifiedHtml})${delimiter}";
`;

await mkdir(path.dirname(outputPath), { recursive: true });
await writeFile(outputPath, header, "utf8");

console.log(`HTML minificado: ${minifiedHtml.length} caracteres`);
console.log(`Generado: ${outputPath}`);
