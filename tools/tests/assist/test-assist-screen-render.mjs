// Host regression for Assist screens from data: the lab draws every screen
// file with the firmware's LVGL 9.6 renderer (lab --png), and each picture is
// compared with its reference in fixtures/assist-screens (128x75 grey, made
// with --update; the nine built-in styles' references come from renders that
// matched the former C++ styles). A second run must give the same pixels (the
// simulated voice and lv_rand start from fixed seeds).
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
if (!(await lvglHost(root))) {
  console.log('SKIP: assist screen render test needs LVGL 9.6.0 (LVGL_INCLUDE) and a host compiler');
  process.exit(0);
}

const build = spawnSync(process.execPath, [path.join(root, 'tools/assist-screen-lab/build.mjs')], {encoding: 'utf8'});
assert.equal(build.status, 0, build.stdout + build.stderr);
assert.ok(!/warning:/.test(build.stdout + build.stderr), build.stdout + build.stderr);
const lab = path.join(root, 'build/assist-screen-lab', process.platform === 'win32' ? 'lab.exe' : 'lab');
const out = path.join(root, 'build/tests/assist-screen-render');
fs.rmSync(out, {recursive: true, force: true});

// The lab writes stored-deflate RGB PNGs (filter 0 on every row).
function readPng(file) {
  const b = fs.readFileSync(file);
  let p = 8, w = 0, h = 0;
  const idat = [];
  while (p < b.length) {
    const len = b.readUInt32BE(p);
    const type = b.toString('latin1', p + 4, p + 8);
    const data = b.subarray(p + 8, p + 8 + len);
    if (type === 'IHDR') { w = data.readUInt32BE(0); h = data.readUInt32BE(4); }
    if (type === 'IDAT') idat.push(data);
    p += 12 + len;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const px = Buffer.alloc(w * h * 3);
  for (let y = 0; y < h; ++y) raw.copy(px, y * w * 3, y * (w * 3 + 1) + 1, (y + 1) * (w * 3 + 1));
  return {w, h, px};
}

const screens = path.join(root, 'tools/assist-screen-lab/screens');
// A 128x75 grey copy of the picture (box average), for the references.
function thumbnail(file) {
  const {w, h, px} = readPng(file);
  const tw = 128, th = 75, out = Buffer.alloc(tw * th);
  for (let ty = 0; ty < th; ++ty) {
    for (let tx = 0; tx < tw; ++tx) {
      let sum = 0, n = 0;
      for (let y = Math.floor(ty * h / th); y < Math.floor((ty + 1) * h / th); ++y) {
        for (let x = Math.floor(tx * w / tw); x < Math.floor((tx + 1) * w / tw); ++x) {
          const p = (y * w + x) * 3;
          sum += (px[p] * 30 + px[p + 1] * 59 + px[p + 2] * 11) / 100;
          ++n;
        }
      }
      out[ty * tw + tx] = Math.round(sum / Math.max(1, n));
    }
  }
  return out;
}
const fixtures = path.join(root, 'tools/tests/assist/fixtures/assist-screens');
const update = process.argv.includes('--update');
if (update) fs.mkdirSync(fixtures, {recursive: true});

const report = [];
for (const size of ['1024x600', '800x480', '480x480', '720x1280']) {
  const dir = path.join(out, size);
  const run = spawnSync(lab, [screens, `--png=${dir}`, `--size=${size}`], {encoding: 'utf8'});
  assert.equal(run.status, 0, run.stdout + run.stderr);
  const all = fs.readdirSync(dir).filter(f => f.endsWith('.png'));
  const shown = fs.readdirSync(screens).filter(f => f.endsWith('.json')).length;
  assert.equal(all.length, shown * 3, `${size}: every screen in three phases`);
  let worstRef = 0;
  for (const f of all) {
    const thumb = thumbnail(path.join(dir, f));
    const ref = path.join(fixtures, f.replace('.png', '.gray.gz'));
    if (update) {
      fs.writeFileSync(ref, zlib.gzipSync(thumb, {level: 9}));
      continue;
    }
    assert.ok(fs.existsSync(ref), `${f}: no reference picture (run this test with --update)`);
    const expected = zlib.gunzipSync(fs.readFileSync(ref));
    let differ = 0;
    for (let i = 0; i < thumb.length; ++i) if (Math.abs(thumb[i] - expected[i]) > 24) ++differ;
    const share = 100 * differ / thumb.length;
    assert.ok(share < 4, `${f}: ${share.toFixed(2)} % of the reference picture differs`);
    worstRef = Math.max(worstRef, share);
  }
  report.push(`${size} worst ${worstRef.toFixed(2)} % vs reference`);
}

// The same run twice gives the same pictures.
const again = path.join(out, 'again');
const rerun = spawnSync(lab, [screens, `--png=${again}`, '--size=1024x600'], {encoding: 'utf8'});
assert.equal(rerun.status, 0, rerun.stdout + rerun.stderr);
for (const f of fs.readdirSync(again)) {
  assert.ok(fs.readFileSync(path.join(again, f)).equals(fs.readFileSync(path.join(out, '1024x600', f))), `${f} repeats`);
}
console.log(`assist screen render ok: ${report.join(', ')}`);
