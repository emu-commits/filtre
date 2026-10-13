// FILTRE arranger - arr.js
// The arrangement itself: which pattern and which mutes every measure of the
// reference track has, in FILTRE's terms, and the song chain that follows
// from it. No DOM here (tools/arranger-test runs it under node).
//
// A document:
//   { name, audio, bpm, downbeat, filter,
//     patterns: { 1: { bars: 1..4, name }, ... },     only the ones touched
//     measures: [ { pat: 0..64, mute: 13 bits, cut? }, ... ] index 0 = measure 1, pat 0 = nothing yet }
// cut is the filter row (f on the device): where the cutoff has got to by the
// end of the section, 0 = shut .. 254 = open. Left out = the filter is not driven.
// filter is the type of that filter, one for the whole project: '' = as set on the device.
//
// FILTRE's limits (config.h / sequencer.h)
export const NUM_PATTERNS = 64;
export const MAX_BARS = 4;                  // bars per pattern
export const SONG_LENGTH = 64;
export const MAX_REPEATS = 16;
export const CUT_MAX = 254;                 // song-slot filter target; the device keeps 255 for "none"
export const cutOf = m => (m && m.pat && m.cut >= 0 && m.cut <= CUT_MAX) ? m.cut : -1;
export const cutPct = c => Math.round(c * 100 / CUT_MAX);
// the project's filter type, in the device's order (deck.h FilterType)
export const FILTERS = [['LP', 'Low pass'], ['HP', 'High pass'], ['BP', 'Band pass'], ['PHASER', 'Phaser']];
export const filterOf = s => { s = String(s || '').toUpperCase(); return FILTERS.some(f => f[0] === s) ? s : ''; };
// the 13 grid rows = the 13 mute keys, bit 0 first
export const ROWS = ['T1', 'T2', 'T3', 'A', 'B', 'D1', 'D2', 'D3', 'D4', 'D5', 'D6', 'D7', 'D8'];
export const ROW_KEYS = ['`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '='];
export const MUTE_MASK = (1 << ROWS.length) - 1;

export function newDoc() {
  return { name: '', audio: '', bpm: 120, downbeat: 0, filter: '', patterns: {}, measures: [] };
}

export function patBars(doc, pat) {
  const p = doc.patterns[pat];
  return clampBars(p && p.bars);
}
export function clampBars(n) {
  n = parseInt(n, 10);
  return n >= 1 && n <= MAX_BARS ? n : 1;
}

// Runs: neighbouring measures with the same pattern, mutes and filter target. Each
// one becomes a song slot (or several: a slot repeats 16 times at most).
// A pattern longer than a bar has to fit its run a whole number of times; where
// it does not, the left-over measures are reported and left out of the song.
export function build(doc) {
  const m = doc.measures, runs = [], issues = [];
  let first = 0, last = m.length - 1;
  while (first < m.length && !m[first].pat) first++;
  while (last >= 0 && !m[last].pat) last--;
  for (let i = first; i <= last; ) {
    let j = i + 1;
    while (j <= last && m[j].pat === m[i].pat && m[j].mute === m[i].mute && cutOf(m[j]) === cutOf(m[i])) j++;
    const pat = m[i].pat, len = j - i, bars = pat ? patBars(doc, pat) : 1;
    const run = { start: i, len, pat, mute: m[i].mute, cut: cutOf(m[i]), bars, reps: Math.floor(len / bars), rest: len % bars };
    runs.push(run);
    if (!pat) issues.push({ at: i, len, text: `Measure${len > 1 ? 's' : ''} ${span(i, len)}: no pattern. The song would skip ${len > 1 ? 'them' : 'it'}.` });
    else if (run.rest) issues.push({ at: i + run.reps * bars, len: run.rest,
      text: `Measure${run.rest > 1 ? 's' : ''} ${span(i + run.reps * bars, run.rest)}: P${pat} is ${bars} bars long but only ${run.rest === 1 ? 'its first bar fits' : `its first ${run.rest} bars fit`} here. Left out of the song.` });
    i = j;
  }
  // The device sweeps the cutoff in a straight line over a slot, to the slot's
  // target. A run longer than 16 repeats is several slots: each gets the point
  // the one long sweep would have reached, when where it starts from is known.
  const slots = [];
  let from = -1;                              // the target the song last reached, -1 = not known
  for (const r of runs) {
    if (!r.pat) continue;
    for (let left = r.reps; left > 0; left -= MAX_REPEATS) {
      const reps = Math.min(left, MAX_REPEATS), done = r.reps - left + reps;
      const cut = r.cut < 0 ? -1 : from < 0 ? r.cut : Math.round(from + (r.cut - from) * done / r.reps);
      slots.push({ pat: r.pat, reps, mute: r.mute, cut, at: r.start + (r.reps - left) * r.bars });
    }
    if (r.cut >= 0) from = r.cut;
  }
  if (slots.length > SONG_LENGTH)
    issues.push({ at: slots[SONG_LENGTH].at, len: 1, text: `The song needs ${slots.length} slots and FILTRE has ${SONG_LENGTH}. It stops at measure ${slots[SONG_LENGTH].at}.` });
  const used = new Set(m.filter(x => x.pat).map(x => x.pat));
  return { runs, slots, issues, first, last, used: [...used].sort((a, b) => a - b) };
}

function span(i, len) { return len > 1 ? `${i + 1}-${i + len}` : `${i + 1}`; }

// where a measure sits inside its run: which bar of the pattern, and whether
// it is a left-over part of a pattern that did not fit whole
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

// The file. FILTRE reads BPM, FILTER, P and S; the rest is ours and it ignores it.
export function serialize(doc) {
  const b = build(doc), out = [];
  out.push('FILTRE-ARR 1');
  out.push('# An arrangement for FILTRE, made with the arranger.');
  out.push('# The device reads BPM, FILTER, P and S lines. Everything else is the tool\'s.');
  if (doc.name) out.push('NAME ' + oneLine(doc.name));
  out.push('BPM ' + Math.round(doc.bpm));
  if (filterOf(doc.filter)) out.push('FILTER ' + filterOf(doc.filter));
  const pats = new Set([...b.used, ...Object.keys(doc.patterns).map(Number)]);
  for (const p of [...pats].sort((x, y) => x - y)) {
    const d = doc.patterns[p] || {};
    out.push(`P ${p} ${clampBars(d.bars)}${d.name ? ' ' + oneLine(d.name) : ''}`);
  }
  for (const s of b.slots.slice(0, SONG_LENGTH)) out.push(`S ${s.pat} ${s.reps} ${hex(s.mute)}${s.cut >= 0 ? ' ' + s.cut : ''}`);
  if (doc.audio) out.push('AUDIO ' + oneLine(doc.audio));
  out.push('TEMPO ' + doc.bpm.toFixed(3));
  out.push('DOWNBEAT ' + doc.downbeat.toFixed(4));
  out.push('MEASURES ' + doc.measures.length);
  for (const r of b.runs) out.push(`M ${r.start + 1} ${r.len} ${r.pat} ${hex(r.mute)}${r.cut >= 0 ? ' ' + r.cut : ''}`);
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
    else if (key === 'FILTER') doc.filter = filterOf(f[0]);
    else if (key === 'TEMPO') tempo = parseFloat(rest) || 0;
    else if (key === 'DOWNBEAT') doc.downbeat = parseFloat(rest) || 0;
    else if (key === 'MEASURES') count = parseInt(rest, 10) || 0;
    else if (key === 'P') {
      const p = parseInt(f[0], 10);
      if (p >= 1 && p <= NUM_PATTERNS) doc.patterns[p] = { bars: clampBars(f[1]), name: f.slice(2).join(' ') };
    } else if (key === 'S') {
      const p = parseInt(f[0], 10), reps = parseInt(f[1], 10);
      if (p >= 1 && p <= NUM_PATTERNS && reps >= 1) slots.push({ pat: p, reps, mute: parseInt(f[2], 16) & MUTE_MASK, cut: readCut(f[3]) });
    } else if (key === 'M') {
      const start = parseInt(f[0], 10), len = parseInt(f[1], 10), p = parseInt(f[2], 10);
      if (start >= 1 && len >= 1 && len < 100000 && p >= 0 && p <= NUM_PATTERNS) runs.push({ start, len, pat: p, mute: parseInt(f[3], 16) & MUTE_MASK, cut: readCut(f[4]) });
    }
  }
  if (tempo) doc.bpm = tempo;                 // BPM is rounded for the device; TEMPO is what the grid was fitted to
  if (runs.length) {
    for (const r of runs)
      for (let i = 0; i < r.len; i++) doc.measures[r.start - 1 + i] = measure(r.pat, r.pat ? r.mute : 0, r.cut);
  } else {
    // a file written by hand or by something else: lay the song out from measure 1
    for (const s of slots)
      for (let i = 0; i < s.reps * patBars(doc, s.pat); i++) doc.measures.push(measure(s.pat, s.mute, s.cut));
  }
  resize(doc, Math.max(count, doc.measures.length));
  return doc;
}

// a measure carries `cut` only when the filter is driven there
function measure(pat, mute, cut) { return pat && cut >= 0 ? { pat, mute, cut } : { pat, mute }; }
function readCut(s) { const c = /^\d+$/.test(s || '') ? parseInt(s, 10) : -1; return c <= CUT_MAX ? c : -1; }

// keep `measures` dense and exactly n long (never dropping one that is in use)
export function resize(doc, n) {
  let need = n;
  for (let i = doc.measures.length - 1; i >= n; i--) if (doc.measures[i] && doc.measures[i].pat) { need = i + 1; break; }
  doc.measures.length = need;
  for (let i = 0; i < need; i++) if (!doc.measures[i]) doc.measures[i] = { pat: 0, mute: 0 };
  return doc;
}
