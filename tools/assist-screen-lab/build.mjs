// Builds the Assist Screen Lab (lab.cpp) against LVGL 9.6.0 and the
// firmware's fonts, the same way the repo's host tests do
// (tools/lib/lvgl-host.mjs), with SDL2 for the window when pkg-config finds
// it (Linux/WSL: sudo apt install libsdl2-dev).
//
// Usage: node tools/assist-screen-lab/build.mjs
// LVGL: LVGL_INCLUDE=<path to lvgl-9.6.0>, or ~/Documents/Arduino/lvgl-9.6.0/lvgl,
// or build/assist-screen-lab/lvgl-9.6.0 (cloned here when git is available).
// Output: build/assist-screen-lab/lab

import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../lib/lvgl-host.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../..');
const out = path.join(root, 'build/assist-screen-lab');
fs.mkdirSync(out, {recursive: true});

const local = path.join(out, 'lvgl-9.6.0');
if (!process.env.LVGL_INCLUDE && fs.existsSync(path.join(local, 'lvgl.h'))) process.env.LVGL_INCLUDE = local;
let host = await lvglHost(root);
if (!host && !fs.existsSync(path.join(local, 'lvgl.h'))) {
  console.log('LVGL 9.6.0 not found: cloning it into build/assist-screen-lab/lvgl-9.6.0');
  const clone = spawnSync('git', ['clone', '--depth', '1', '--branch', 'v9.6.0', 'https://github.com/lvgl/lvgl.git', local],
    {stdio: 'inherit'});
  if (clone.status === 0) {
    process.env.LVGL_INCLUDE = local;
    host = await lvglHost(root);
  }
}
if (!host) {
  console.error('Needs LVGL 9.6.0 (LVGL_INCLUDE), a C/C++ compiler (clang or gcc) and ar.');
  process.exit(1);
}

// The firmware headers the built-in styles include want Arduino.h.
const stubs = path.join(out, 'stubs');
fs.mkdirSync(stubs, {recursive: true});
fs.writeFileSync(path.join(stubs, 'Arduino.h'), '#pragma once\n#include <stdint.h>\n#include <stddef.h>\n');

const sdl = spawnSync('pkg-config', ['--cflags', '--libs', 'sdl2'], {encoding: 'utf8'});
const sdlFlags = sdl.status === 0 ? sdl.stdout.trim().split(/\s+/) : ['-DLAB_NO_SDL'];
if (sdl.status !== 0) console.log('SDL2 not found (pkg-config sdl2): building the headless lab only (--png).');

const binary = path.join(out, process.platform === 'win32' ? 'lab.exe' : 'lab');
const args = [...host.flags, '-std=c++17', '-O2', '-Wall', '-Wno-unused-function', '-Wno-missing-field-initializers', '-Wno-deprecated-declarations',
  '-I', stubs, '-I', here, `-DLAB_SCREENS_DIR="${path.join(here, 'screens').replace(/\\/g, '/')}"`,
  path.join(here, 'lab.cpp'), path.join(here, 'engine/assist_screen.cpp'), host.archive,
  ...sdlFlags, '-lm', '-o', binary];
const build = spawnSync(host.cxx, args, {encoding: 'utf8'});
if (build.status !== 0) {
  console.error(build.stdout + build.stderr);
  process.exit(1);
}
if (build.stderr.trim()) console.log(build.stderr.trim());
console.log(`Built ${path.relative(root, binary)}${sdl.status === 0 ? ' (window and headless)' : ' (headless only)'}`);
