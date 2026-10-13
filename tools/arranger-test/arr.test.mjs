// node tools/arranger-test/arr.test.mjs
import { newDoc, build, serialize, parse, place, resize, muteNames } from '../../docs/arranger/arr.js';
import { readFileSync } from 'node:fs';
let pass = 0, fail = 0;
const check = (ok, msg) => { if (ok) pass++; else { fail++; console.log('  FAIL ' + msg); } };
const fill = (doc, from, n, pat, mute = 0) => { for (let i = 0; i < n; i++) doc.measures[from - 1 + i] = { pat, mute }; };

{ // runs become slots; mutes split a run; repeats come from the pattern's length
  const d = newDoc(); resize(d, 70);
  d.patterns[1] = { bars: 2, name: 'main loop' };
  fill(d, 3, 8, 1, 0x1FE0); fill(d, 11, 8, 1, 0); fill(d, 19, 3, 2, 1); fill(d, 22, 40, 1, 0);
  const b = build(d);
  check(b.first === 2 && b.last === 60, `silent measures before and after are left alone (${b.first} ${b.last})`);
  check(b.slots.length === 5, `slots (${b.slots.length})`);
  check(b.slots[0].pat === 1 && b.slots[0].reps === 4 && b.slots[0].mute === 0x1FE0, '8 measures of a 2-bar pattern = x4');
  check(b.slots[1].reps === 4 && b.slots[1].mute === 0, 'the same pattern with other mutes is its own slot');
  check(b.slots[2].pat === 2 && b.slots[2].reps === 3, '1-bar pattern: one repeat per measure');
  check(b.slots[3].reps === 16 && b.slots[4].reps === 4 && b.slots[4].at === 21 + 32, 'x20 is split 16 + 4');
  check(b.issues.length === 0, 'nothing to report');
  check(place(b.runs, 4).bar === 0 && place(b.runs, 5).bar === 1 && place(b.runs, 5).run.pat === 1, 'bar 1 / bar 2 of the pattern');
  check(muteNames(0x1001).join() === 'T1,D8', 'mute names');

  const text = serialize(d);
  check(/^FILTRE-ARR 1\n/.test(text) && text.includes('\nBPM 120\n') && text.includes('\nP 1 2 main loop\n') && text.includes('\nS 1 4 1FE0\n') && text.includes('\nS 1 16 0000\nS 1 4 0000\n'), 'the lines FILTRE reads');
  const back = parse(text);
  check(JSON.stringify(back.measures) === JSON.stringify(d.measures) && back.patterns[1].bars === 2 && back.patterns[1].name === 'main loop', 'save then load: the same document');
}
{ // a two-bar pattern that does not fit, a hole, too many slots
  const d = newDoc(); resize(d, 12);
  d.patterns[5] = { bars: 2, name: '' };
  fill(d, 1, 3, 5); fill(d, 5, 2, 6);
  const b = build(d);
  check(b.slots.length === 2 && b.slots[0].reps === 1, 'the odd measure is left out');
  check(b.issues.length === 2 && b.issues[0].at === 2 && b.issues[1].at === 3, `both problems reported where they are (${b.issues.map(i => i.at)})`);
  check(place(b.runs, 2).orphan && !place(b.runs, 1).orphan, 'the left-over measure is marked');
  const big = newDoc(); resize(big, 140);
  for (let i = 0; i < 140; i++) big.measures[i] = { pat: 1 + (i % 2), mute: 0 };
  const bb = build(big);
  check(bb.slots.length === 140 && bb.issues.length === 1 && (serialize(big).match(/\nS /g) || []).length === 64, 'more than 64 slots: reported, file holds 64');
}
{ // tempo keeps its decimals for the grid; the device gets a whole number
  const d = newDoc(); d.bpm = 121.296; d.downbeat = 0.7301; d.name = 'x'; d.audio = 'a b.mp3'; resize(d, 4); fill(d, 1, 4, 1);
  const t = serialize(d), back = parse(t);
  check(t.includes('\nBPM 121\n') && back.bpm === 121.296 && Math.abs(back.downbeat - 0.7301) < 1e-9 && back.audio === 'a b.mp3', 'tempo and downbeat survive');
}
{ // a file with only device lines (written by hand)
  const d = parse('FILTRE-ARR 1\nBPM 126\nP 1 2\nS 1 4 0\nS 2 2 41\n');
  check(d.measures.length === 10 && d.measures[7].pat === 1 && d.measures[9].pat === 2 && d.measures[9].mute === 0x41 && d.bpm === 126, 'laid out from the slots');
  let threw = false; try { parse('hello'); } catch { threw = true; }
  check(threw, 'anything else is refused');
}
{ // the example that ships on the card says what it claims
  const text = readFileSync(new URL('../../factory-sd/groovebox/songs/EXAMPLE.arr', import.meta.url), 'utf8');
  const d = parse(text), again = serialize(d);
  const dev = t => t.split('\n').filter(l => /^(BPM|P|S) /.test(l)).join('\n');
  check(d.measures.length === 64 && build(d).issues.length === 0 && dev(text) === dev(again), 'EXAMPLE.arr: its slots are what its measures add up to');
}
console.log(`${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
