// node tools/arranger-test/ui.test.mjs <track.mp3> [shots-dir]
// Drives the real page in headless Chromium: loads a track, marks measures
// with mouse and keys, saves, reloads the file. Needs playwright (global).
import { createRequire } from 'node:module';
import { createServer } from 'node:http';
import { readFileSync, existsSync, mkdirSync } from 'node:fs';
import { extname, join } from 'node:path';
import { execSync } from 'node:child_process';
const require = createRequire(join(execSync('npm root -g').toString().trim(), 'x.js'));
const { chromium } = require('playwright');

const root = new URL('../../docs/', import.meta.url).pathname;
const track = process.argv[2], shots = process.argv[3];
if (shots) mkdirSync(shots, { recursive: true });
const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css' };
const server = createServer((req, res) => {
  let p = join(root, decodeURIComponent(req.url.split('?')[0]));
  if (p.endsWith('/')) p += 'index.html';
  if (!existsSync(p)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'content-type': types[extname(p)] || 'application/octet-stream' }); res.end(readFileSync(p));
}).listen(0);
const url = `http://localhost:${server.address().port}/arranger/`;

let pass = 0, fail = 0;
const check = (ok, msg) => { if (ok) pass++; else { fail++; console.log('  FAIL ' + msg); } };
const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1400, height: 900 }, acceptDownloads: true });
const errors = [];
page.on('pageerror', e => errors.push(String(e)));
page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
const shot = async n => { if (shots) await page.screenshot({ path: join(shots, n + '.png'), fullPage: false }); };

await page.goto(url);
check(await page.locator('#grid.empty').count() === 1, 'starts empty');
await shot('1-empty');

await page.setInputFiles('#fileAudio', track);
await page.waitForFunction(() => window.__arranger.doc.measures.length > 8, null, { timeout: 60000 });
const info = await page.evaluate(() => ({ n: window.__arranger.doc.measures.length, bpm: window.__arranger.doc.bpm, d: window.__arranger.doc.downbeat }));
console.log(`track: ${info.bpm} BPM, measure 1 at ${info.d.toFixed(3)} s, ${info.n} measures; notice: ${await page.locator('#notice').textContent()}`);
check(await page.locator('.m').count() === info.n, 'one cell per measure');

// measures 1-8: click, shift-click, pattern 1, two bars, kick muted with the FILTRE key
await page.locator('.m').nth(0).click();
await page.locator('.m').nth(7).click({ modifiers: ['Shift'] });
await page.locator('#pats button[data-p="1"]').click();
await page.locator('#bars2').click();
await page.fill('#patName', 'main loop'); await page.keyboard.press('Enter');
await page.keyboard.press('5');                         // D1
// measures 9-16 by drag: same pattern, everything playing
const a = await page.locator('.m').nth(8).boundingBox(), b = await page.locator('.m').nth(15).boundingBox();
await page.mouse.move(a.x + 5, a.y + 5); await page.mouse.down(); await page.mouse.move(b.x + 5, b.y + 5, { steps: 6 }); await page.mouse.up();
await page.keyboard.press(']');
// measure 17 alone, pattern 2 with the arrow + ] keys, T1 muted by button
await page.keyboard.press('ArrowRight'); await page.keyboard.press(']'); await page.keyboard.press(']');
await page.locator('#parts button').nth(0).click();
// measures 18-20: an odd count of a two-bar pattern -> one issue
await page.locator('.m').nth(17).click(); await page.locator('.m').nth(19).click({ modifiers: ['Shift'] });
await page.locator('#pats button[data-p="1"]').click();
await shot('2-marked');

const st = await page.evaluate(() => ({ slots: window.__arranger.built.slots, issues: window.__arranger.built.issues.map(i => i.text), text: window.__arranger.serialize() }));
check(st.slots.length === 4, `four slots (${JSON.stringify(st.slots)})`);
check(st.slots[0].pat === 1 && st.slots[0].reps === 4 && st.slots[0].mute === 0x20, 'slot 1: P1 x4, D1 muted');
check(st.slots[1].pat === 1 && st.slots[1].reps === 4 && st.slots[1].mute === 0, 'slot 2: P1 x4, all playing (drag + ])');
check(st.slots[2].pat === 2 && st.slots[2].reps === 1 && st.slots[2].mute === 1, 'slot 3: P2 x1, T1 muted');
check(st.slots[3].pat === 1 && st.slots[3].reps === 1 && st.issues.length === 1, `slot 4 + the odd measure is reported (${st.issues})`);
check(await page.locator('.m.orphan').count() === 1 && await page.locator('.m.joinR').count() === 9, 'two-bar pattern drawn across two measures, the half one flagged');
check(await page.locator('#song li').count() === 4 && await page.locator('#issues li').count() === 1, 'song list and issue list on screen');

// undo the last assignment, redo it properly, save
await page.keyboard.press('Control+z');
check(await page.evaluate(() => window.__arranger.built.slots.length) === 3, 'undo');
await page.locator('.m').nth(17).click(); await page.locator('.m').nth(20).click({ modifiers: ['Shift'] });
await page.locator('#pats button[data-p="1"]').click();
const [dl] = await Promise.all([page.waitForEvent('download'), page.locator('#saveArr').click()]);
const saved = readFileSync(await dl.path(), 'utf8');
if (process.env.DEBUG) console.log(saved);
console.log(`saved ${dl.suggestedFilename()}:\n${saved.split('\n').filter(l => /^(BPM|P|S) /.test(l)).join(' | ')}`);
check(/\.arr$/.test(dl.suggestedFilename()) && saved.includes('\nS 1 4 0020\nS 1 4 0000\nS 2 1 0001\nS 1 2 0000\n') && saved.includes('P 1 2 main loop'), 'the saved file');

// play, loop, click: no errors, the clock moves
await page.locator('.m').nth(0).click();
await page.locator('#click').check(); await page.locator('#loop').check();
await page.keyboard.press(' ');
await page.waitForTimeout(700);
check(await page.locator('#play').textContent() === 'Stop' && await page.locator('#clock').textContent() !== '0:00.0', 'plays');
await shot('3-playing');
await page.keyboard.press(' ');

// bar-line controls keep the marks on the same music
if (!await page.locator('#tempoBox').evaluate(d => d.open)) await page.locator('#tempoBox summary').click();   // the page opens it itself on a shaky tempo
await page.locator('[data-g="bar+"]').click();
check(await page.evaluate(() => window.__arranger.doc.measures[0].pat === 1 && window.__arranger.doc.measures[15].pat === 2), 'measure 1 a bar later: marks move with the audio');
await page.locator('[data-g="bar-"]').click();
check(await page.evaluate(() => window.__arranger.doc.measures[16].pat === 2), 'and back');
await shot('4-barlines');

// a fresh page picks the work up; a saved file loads without the audio
const page2 = await browser.newPage({ viewport: { width: 420, height: 900 } });
page2.on('pageerror', e => errors.push(String(e)));
await page2.goto(url);
await page2.setInputFiles('#fileArr', { name: 'x.arr', mimeType: 'text/plain', buffer: Buffer.from(saved) });
await page2.waitForFunction(() => window.__arranger.doc.measures.length > 8);
check(await page2.evaluate(() => window.__arranger.serialize()) === saved, 'open the saved file: identical when saved again');
if (shots) await page2.screenshot({ path: join(shots, '5-phone.png') });
check(await page2.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth), 'phone width: no sideways scroll');

// pattern 64, four bars: picking it on one measure takes the three after it
if (info.n >= 34) {
  check(await page.locator('#pats button').count() === 65, '64 patterns to pick from');
  await page.locator('.m').nth(24).click();
  await page.locator('#pats button[data-p="64"]').click();
  await page.locator('#bars4').click();
  const one = await page.evaluate(() => window.__arranger.built.issues.map(i => i.text));
  check(one.some(t => /^Measure 25: P64 is 4 bars long but only its first bar fits/.test(t)), `one measure of a 4-bar pattern is reported (${one})`);
  await page.locator('.m').nth(29).click();
  await page.locator('#pats button[data-p="64"]').click();
  const four = await page.evaluate(() => ({ m: window.__arranger.doc.measures.slice(28, 34).map(x => x.pat), text: window.__arranger.serialize() }));
  check(JSON.stringify(four.m) === '[0,64,64,64,64,0]' && four.text.includes('\nP 64 4\n') && four.text.includes('\nS 64 1 0000\n'), `it spans four measures and saves as one slot (${four.m})`);
  check(await page.locator('.m.bar2.joinL.joinR').count() === 2 && await page.locator('#bars4.cur').count() === 1, 'drawn as one piece, bar 2 and 3 joined both ways');
  await shot('6-four-bars');
}

// the filter row: keys as on FILTRE, the buttons, and it is in the saved file
await page.locator('.m').nth(0).click(); await page.locator('.m').nth(7).click({ modifiers: ['Shift'] });
await page.locator('#cutShut').click();
await page.locator('.m').nth(8).click(); await page.locator('.m').nth(15).click({ modifiers: ['Shift'] });
await page.locator('#cutOpen').click();
await page.keyboard.press('e');                          // 254 - 8
await page.locator('.m').nth(16).click();
await page.keyboard.press('u');                          // from nothing: the middle
const fl = await page.evaluate(() => ({ cuts: window.__arranger.built.slots.slice(0, 3).map(s => s.cut), text: window.__arranger.serialize() }));
check(JSON.stringify(fl.cuts) === '[0,246,127]', `shut, nearly open, half (${fl.cuts})`);
// (measure 1 lost its pattern in the bar-line step above, so the first section is 7 measures = x3)
check(fl.text.includes('\nS 1 3 0020 0\nS 1 4 0000 246\nS 2 1 0001 127\n'), 'filter targets in the S lines');
check(await page.locator('.m .cut.on').count() === 16 && await page.locator('#cutVal').textContent() === '50%', 'every driven measure shows its filter bar; the inspector reads 50%');
await page.keyboard.press('e'); for (let i = 0; i < 20; i++) await page.keyboard.press('e');
check(await page.evaluate(() => !('cut' in window.__arranger.doc.measures[16])) && await page.locator('#cutVal').textContent() === 'not driven', 'e below shut = not driven');
await page.locator('#ftype button', { hasText: 'Phaser' }).click();
check(await page.evaluate(() => window.__arranger.serialize().includes('\nFILTER PHASER\n')) && await page.locator('#ftype button.cur').textContent() === 'Phaser', 'the filter type is in the file');
await page.locator('.m').nth(8).click();
await shot('7-filter');

check(errors.length === 0, 'no page errors: ' + errors.join(' / '));
console.log(`${pass} passed, ${fail} failed`);
await browser.close(); server.close();
process.exit(fail ? 1 : 0);
