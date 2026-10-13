// FILTRE arranger - arr.js
// The arrangement itself: which pattern and which mutes every measure of the
// reference track has, in FILTRE's terms, and the song chain that follows
// from it. No DOM here (tools/arranger-test runs it under node).
//
// A document:
//   { name, audio, bpm, downbeat,
//     patterns: { 1: { bars: 1|2, name }, ... },      only the ones touched
//     measures: [ { pat: 0..32, mute: 13 bits }, ... ] index 0 = measure 1, pat 0 = nothing yet }
//
// FILTRE's limits (config.h / sequencer.h)
export const NUM_PATTERNS = 32;
export const SONG_LENGTH = 64;
export const MAX_REPEATS = 16;
// the 13 grid rows = the 13 mute keys, bit 0 first
export const ROWS = ['T1', 'T2', 'T3', 'A', 'B', 'D1', 'D2', 'D3', 'D4', 'D5', 'D6', 'D7', 'D8'];
export const ROW_KEYS = ['`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '='];
export const MUTE_MASK = (1 << ROWS.length) - 1;

export function newDoc() {
  return { name: '', audio: '', bpm: 120, downbeat: 0, patterns: {}, measures: [] };
}

export function patBars(doc, pat) {
  const p = doc.patterns[pat];
  return p && p.bars === 2 ? 2 : 1;
}

// Runs: neighbouring measures with the same pattern and the same mutes. Each
// one becomes a song slot (or several: a slot repeats 16 times at most).
// A two-bar pattern has to fit its run a whole number of times; where it does
// not, the left-over measure is reported and left out of the song.
export function build(doc) {
  const m = doc.measures, runs = [], issues = [];
  let first = 0, last = m.length - 1;
  while (first < m.length && !m[first].pat) first++;
  while (last >= 0 && !m[last].pat) last--;
  for (let i = first; i <= last; ) {
    let j = i + 1;
    while (j <= last && m[j].pat === m[i].pat && m[j].mute === m[i].mute) j++;
    const pat = m[i].pat, len = j - i, bars = pat ? patBars(doc, pat) : 1;
    const run = { start: i, len, pat, mute: m[i].mute, bars, reps: Math.floor(len / bars), rest: len % bars };
    runs.push(run);
    if (!pat) issues.push({ at: i, len, text: `Measure${len > 1 ? 's' : ''} ${span(i, len)}: no pattern. The song would skip ${len > 1 ? 'them' : 'it'}.` });
    else if (run.rest) issues.push({ at: i + len - 1, len: 1, text: `Measure ${i + len}: P${pat} is 2 bars long but only its first bar fits here. Left out of the song.` });
    i = j;
  }
  const slots = [];
  for (const r of runs) {
    if (!r.pat) continue;
    for (let left = r.reps; left > 0; left -= MAX_REPEATS)
      slots.push({ pat: r.pat, reps: Math.min(left, MAX_REPEATS), mute: r.mute, at: r.start + (r.reps - left) * r.bars });
  }
  if (slots.length > SONG_LENGTH)
    issues.push({ at: slots[SONG_LENGTH].at, len: 1, text: `The song needs ${slots.length} slots and FILTRE has ${SONG_LENGTH}. It stops at measure ${slots[SONG_LENGTH].at}.` });
  const used = new Set(m.filter(x => x.pat).map(x => x.pat));
  return { runs, slots, issues, first, last, used: [...used].sort((a, b) => a - b) };
}

function span(i, len) { return len > 1 ? `${i + 1}-${i + len}` : `${i + 1}`; }

// where a measure sits inside its run: which bar of the pattern, and whether
// it is the left-over half of a two-bar pattern
export function place(runs, i) {
  for (const r of runs) {
    if (i < r.start || i >= r.start + r.len) continue;
    const k = i - r.start;
    return { run: r, bar: k % r.bars, orphan: r.pat > 0 && k >= r.reps * r.bars, head: k % r.bars === 0 };
  }
  return null;
}

const hex = n => (n & MUTE_MASK).toString(16).toUpperCase().padStart(4, '0');

export function muteNames(mute) {
  return ROWS.filter((_, b) => mute & (1 << b));
}

// The file. FILTRE reads BPM, P and S; the rest is ours and it ignores it.
export function serialize(doc) {
  const b = build(doc), out = [];
  out.push('FILTRE-ARR 1');
  out.push('# An arrangement for FILTRE, made with the arranger.');
  out.push('# The device reads BPM, P and S lines. Everything else is the tool\'s.');
  if (doc.name) out.push('NAME ' + oneLine(doc.name));
  out.push('BPM ' + Math.round(doc.bpm));
  const pats = new Set([...b.used, ...Object.keys(doc.patterns).map(Number)]);
  for (const p of [...pats].sort((x, y) => x - y)) {
    const d = doc.patterns[p] || {};
    out.push(`P ${p} ${d.bars === 2 ? 2 : 1}${d.name ? ' ' + oneLine(d.name) : ''}`);
  }
  for (const s of b.slots.slice(0, SONG_LENGTH)) out.push(`S ${s.pat} ${s.reps} ${hex(s.mute)}`);
  if (doc.audio) out.push('AUDIO ' + oneLine(doc.audio));
  out.push('TEMPO ' + doc.bpm.toFixed(3));
  out.push('DOWNBEAT ' + doc.downbeat.toFixed(4));
  out.push('MEASURES ' + doc.measures.length);
  for (const r of b.runs) out.push(`M ${r.start + 1} ${r.len} ${r.pat} ${hex(r.mute)}`);
  return out.join('\n') + '\n';
}

function oneLine(s) { return String(s).replace(/[\r\n]+/g, ' ').trim(); }

export function parse(text) {
  const lines = String(text).split(/\r?\n/).map(l => l.trim()).filter(l => l && l[0] !== '#');
  if (!lines.length || !lines[0].startsWith('FILTRE-ARR')) throw new Error('Not a FILTRE arrangement (.arr) file.');
  const doc = newDoc(), slots = [], runs = [];
  let tempo = 0, count = 0;
  for (const l of lines.slice(1)) {
    const sp = l.indexOf(' '), key = sp < 0 ? l : l.slice(0, sp), rest = sp < 0 ? '' : l.slice(sp + 1).trim();
    const f = rest.split(/\s+/);
    if (key === 'NAME') doc.name = rest;
    else if (key === 'AUDIO') doc.audio = rest;
    else if (key === 'BPM') doc.bpm = parseFloat(rest) || doc.bpm;
    else if (key === 'TEMPO') tempo = parseFloat(rest) || 0;
    else if (key === 'DOWNBEAT') doc.downbeat = parseFloat(rest) || 0;
    else if (key === 'MEASURES') count = parseInt(rest, 10) || 0;
    else if (key === 'P') {
      const p = parseInt(f[0], 10);
      if (p >= 1 && p <= NUM_PATTERNS) doc.patterns[p] = { bars: f[1] === '2' ? 2 : 1, name: f.slice(2).join(' ') };
    } else if (key === 'S') {
      const p = parseInt(f[0], 10), reps = parseInt(f[1], 10);
      if (p >= 1 && p <= NUM_PATTERNS && reps >= 1) slots.push({ pat: p, reps, mute: parseInt(f[2], 16) & MUTE_MASK });
    } else if (key === 'M') {
      const start = parseInt(f[0], 10), len = parseInt(f[1], 10), p = parseInt(f[2], 10);
      if (start >= 1 && len >= 1 && len < 100000 && p >= 0 && p <= NUM_PATTERNS) runs.push({ start, len, pat: p, mute: parseInt(f[3], 16) & MUTE_MASK });
    }
  }
  if (tempo) doc.bpm = tempo;                 // BPM is rounded for the device; TEMPO is what the grid was fitted to
  if (runs.length) {
    for (const r of runs)
      for (let i = 0; i < r.len; i++) doc.measures[r.start - 1 + i] = { pat: r.pat, mute: r.pat ? r.mute : 0 };
  } else {
    // a file written by hand or by something else: lay the song out from measure 1
    for (const s of slots)
      for (let i = 0; i < s.reps * patBars(doc, s.pat); i++) doc.measures.push({ pat: s.pat, mute: s.mute });
  }
  resize(doc, Math.max(count, doc.measures.length));
  return doc;
}

// keep `measures` dense and exactly n long (never dropping one that is in use)
export function resize(doc, n) {
  let need = n;
  for (let i = doc.measures.length - 1; i >= n; i--) if (doc.measures[i] && doc.measures[i].pat) { need = i + 1; break; }
  doc.measures.length = need;
  for (let i = 0; i < need; i++) if (!doc.measures[i]) doc.measures[i] = { pat: 0, mute: 0 };
  return doc;
}
