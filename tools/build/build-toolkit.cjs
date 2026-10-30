#!/usr/bin/env node
// Bundle the toolkit against stock React, with Kindle's native component lookup.
const fs = require('node:fs');
const path = require('node:path');
const { parseArgs } = require('node:util');
const esbuild = require('esbuild');
const {
  toolkitSource,
  sharedRuntime,
  compileForHermes
} = require('./shared-runtime.cjs');
const { values, positionals } = parseArgs({
  options: { toolkit: { type: 'string' } },
  allowPositionals: true
});
if (positionals.length !== 1)
  throw new Error(
    'Usage: npm run build:toolkit -- OUTPUT [--toolkit REACT_KINDLE_CHECKOUT]'
  );
const [output] = positionals;

async function build() {
  const result = await esbuild.build({
    entryPoints: [path.join(toolkitSource(values.toolkit), 'index.js')],
    bundle: true,
    write: false,
    format: 'iife',
    globalName: 'toolkit',
    jsx: 'automatic',
    loader: { '.js': 'jsx' },
    target: 'es2015',
    plugins: [
      sharedRuntime({ react: 'KPPMods.stock("react")', 'react-native': 'embeddedRN' })
    ]
  });
  fs.writeFileSync(
    output,
    `KPPMods.loadToolkit = function () {
    var stockRN = KPPMods.stock("reactNative");
    var embeddedRN = Object.create(stockRN);
    var dimensions = Object.create(stockRN.Dimensions);
    Object.defineProperty(dimensions, "addEventListener", { value: function (event, listener) {
      var subscription = stockRN.Dimensions.addEventListener(event, listener);
      return subscription || { remove: function () {
        stockRN.Dimensions.removeEventListener(event, listener);
      } };
    } });
    Object.defineProperty(embeddedRN, "Dimensions", { value: dimensions });
    Object.defineProperty(embeddedRN, "requireNativeComponent", { value: function (name) { return name; } });
    ${compileForHermes(result.outputFiles[0].text)}
    return toolkit;
  };\n`
  );
}
build().catch((error) => {
  console.error(error.message);
  process.exitCode = 1;
});
