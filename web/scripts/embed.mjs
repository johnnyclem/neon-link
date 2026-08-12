/**
 * Gzips the built bundle into the firmware component.
 *
 * The compressed artifact is committed. That is deliberate: `idf.py build`
 * has to work from a bare checkout with no Node toolchain present, which is
 * exactly what CI and anyone flashing the module does. CI rebuilds it and
 * fails if the committed copy has drifted, so "committed artifact" never
 * turns into "stale artifact".
 */

import { gzipSync } from "node:zlib";
import { mkdirSync, readFileSync, writeFileSync, statSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const source = resolve(here, "../dist/index.html");
const target = resolve(here, "../../components/web_ui/www/dist/index.html.gz");

// A rough ceiling, not a hard resource limit: the app partition has 3 MB
// free. It exists so a careless dependency shows up as a failed build rather
// than as a slow first load over the setup access point.
const BUDGET_BYTES = 60 * 1024;

const html = readFileSync(source);
// Maximum level: this is compressed once at build time and decompressed by
// the browser, so there is no runtime cost to trade against.
const gz = gzipSync(html, { level: 9 });

mkdirSync(dirname(target), { recursive: true });
writeFileSync(target, gz);

const kb = (n) => `${(n / 1024).toFixed(1)} kB`;
console.log(`  bundle    ${kb(html.length)}`);
console.log(`  gzipped   ${kb(gz.length)}  ->  ${target.replace(/.*\/neon-link\//, "")}`);

if (gz.length > BUDGET_BYTES) {
  console.error(
    `\nBundle is ${kb(gz.length)} gzipped, over the ${kb(BUDGET_BYTES)} budget.`,
  );
  process.exit(1);
}

// Sanity: the firmware embeds this file by symbol, so an empty or truncated
// artifact would flash cleanly and serve a blank page.
if (statSync(target).size < 1024) {
  console.error("\nGzipped bundle is implausibly small — refusing to ship it.");
  process.exit(1);
}
