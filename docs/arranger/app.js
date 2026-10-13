// FILTRE arranger - app.js
// The page: load a track, lay its bars out, mark each one with a FILTRE
// pattern and the parts that are silent, save the song. The model lives in
// arr.js and the bar-line detection in beat.js; this file is glue and DOM.
import { analyse, monoMix } from './beat.js';
import { NUM_PATTERNS, SONG_LENGTH, ROWS, ROW_KEYS, MUTE_MASK,
         newDoc, patBars, build, place, serialize, parse, resize, muteNames } from './arr.js';

const $ = id => document.getElementById(id);
const STORE = 'filtre-arranger';
const PART_COLOR = ['--t1', '--t2', '--t3', '--da', '--db', '--dr', '--dr', '--dr', '--dr', '--dr', '--dr', '--dr', '--dr'];
const patColor = p => `hsl(${Math.round(p * 137.5 + 290) % 360} 72% 62%)`;   // golden angle: neighbours differ

let doc = newDoc();
let built = build(doc);
let audio = null;            // { buf, mono, duration, name }
let sel = null;              // { a, b } measure indices, anchor and focus
let undo = [];
let cells = [];              // DOM per measure
let waveQueue = 0;

// ---------- time ----------
const beatSec = () => 60 / doc.bpm;
const barSec = () => 240 / doc.bpm;
const measureStart = i => doc.downbeat + i * barSec();
const measureAt = t => Math.floor((t - doc.downbeat) / barSec() + 1e-6);
const selRange = () => sel ? [Math.min(sel.a, sel.b), Math.max(sel.a, sel.b)] : null;

function measureCount() {
  if (!audio) return doc.measures.length;
  return Math.max(1, Math.ceil((audio.duration - doc.downbeat) / barSec() - 0.05));
}

// ---------- changes ----------
function snapshot() {
  undo.push(JSON.stringify({ m: doc.measures, p: doc.patterns, bpm: doc.bpm, d: doc.downbeat }));
  if (undo.length > 200) undo.shift();
}
function undoLast() {
  const s = undo.pop();
  if (!s) return;
  const o = JSON.parse(s);
  doc.measures = o.m; doc.patterns = o.p; doc.bpm = o.bpm; doc.downbeat = o.d;
  layout();
}
function save() {
  try { localStorage.setItem(STORE, serialize(doc)); } catch { /* private window: nothing to keep */ }
}
// after anything that changes patterns or mutes
function changed() {
  built = build(doc);
  for (let i = 0; i < cells.length; i++) paintCell(i);
  paintInspector(); paintSong(); drawOverview(); save();
}
// after anything that moves the bar lines or the number of measures
function layout() {
  resize(doc, measureCount());
  if (sel) { sel.a = Math.min(sel.a, doc.measures.length - 1); sel.b = Math.min(sel.b, doc.measures.length - 1); }
  buildGrid();
  $('bpm').value = String(Math.round(doc.bpm * 1000) / 1000);
  $('tempoSum').textContent = doc.measures.length
    ? `${doc.bpm.toFixed(2)} BPM, measure 1 at ${doc.downbeat.toFixed(3)} s` : '';
  $('gridInfo').textContent = doc.measures.length ? `${doc.measures.length} measures` : '';
  $('add8').hidden = !!audio;
  changed();
  if (playing) restartAt(position());
}

// ---------- the grid ----------
function buildGrid() {
  const g = $('grid');
  g.textContent = '';
  cells = [];
  if (!doc.measures.length) {
    g.className = 'grid empty';
    g.innerHTML = '<p><b>Open a track</b> (MP3, WAV, M4A...) or drop it here.</p><p>It is cut into measures on the bar lines. Mark each one with the FILTRE pattern it would be and the parts that are silent, then save the song for FILTRE to load.</p>';
    return;
  }
  g.className = 'grid';
  const frag = document.createDocumentFragment();
  for (let i = 0; i < doc.measures.length; i++) {
    const el = document.createElement('div');
    el.className = 'm none';
    el.dataset.i = i;
    el.tabIndex = -1;
    el.innerHTML = `<canvas height="40"></canvas><div class="head"></div><div class="num">${i + 1}</div><div class="pat"></div><div class="rows">${'<i></i>'.repeat(ROWS.length)}</div>`;
    const dots = el.querySelectorAll('.rows i');
    dots.forEach((d, b) => d.style.setProperty('--pc', `var(${PART_COLOR[b]})`));
    cells.push({ el, canvas: el.firstChild, head: el.children[1], pat: el.children[3], dots, drawn: '' });
    frag.appendChild(el);
  }
  g.appendChild(frag);
  queueWaves();
}

function paintCell(i) {
  const c = cells[i], m = doc.measures[i];
  if (!c || !m) return;
  const pl = place(built.runs, i);
  const [lo, hi] = selRange() || [-1, -1];
  let cls = 'm';
  if (!m.pat) cls += ' none';
  if (i >= lo && i <= hi) cls += ' sel';
  if (i === nowCell) cls += ' now';
  let label = '';
  if (m.pat && pl) {
    const two = pl.run.bars === 2;
    if (pl.orphan) { cls += ' orphan'; label = `P${m.pat} <small>half</small>`; }
    else if (two && pl.bar === 1) { cls += ' bar2 joinL'; label = '<small>bar 2</small>'; }
    else {
      if (two) cls += ' joinR';
      const nm = doc.patterns[m.pat] && doc.patterns[m.pat].name;
      label = `P${m.pat}${nm && pl.run.start === i ? ` <small>${esc(nm)}</small>` : ''}`;
    }
    if (pl.run.start === i) cls += ' sectionStart';
  } else if (!m.pat) label = '&ndash;';
  if (c.el.className !== cls) c.el.className = cls;
  if (c.pat.innerHTML !== label) c.pat.innerHTML = label;
  c.el.style.setProperty('--c', m.pat ? patColor(m.pat) : 'var(--line2)');
  for (let b = 0; b < ROWS.length; b++) c.dots[b].className = (m.pat && !(m.mute & (1 << b))) ? 'on' : '';
  const tint = m.pat ? patColor(m.pat) : '';
  if (c.tint !== tint) { c.tint = tint; c.drawn = ''; queueWaves(); }
}

function esc(s) { return String(s).replace(/[&<>"]/g, ch => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[ch])); }

// waveforms are drawn a few at a time so a long track does not lock the page
function queueWaves() {
  if (waveQueue) return;
  waveQueue = requestAnimationFrame(function work() {
    waveQueue = 0;
    const t0 = performance.now();
    for (let i = 0; i < cells.length; i++) {
      const key = `${doc.bpm}|${doc.downbeat}|${cells[i].tint || ''}|${cells[i].canvas.clientWidth}|${audio ? audio.name : ''}`;
      if (cells[i].drawn === key) continue;
      drawWave(i); cells[i].drawn = key;
      if (performance.now() - t0 > 12) { waveQueue = requestAnimationFrame(work); return; }
    }
  });
}

function drawWave(i) {
  const c = cells[i], cv = c.canvas, dpr = Math.min(2, window.devicePixelRatio || 1);
  const w = Math.max(8, Math.round(cv.clientWidth * dpr)), h = Math.round(40 * dpr);
  if (cv.width !== w) cv.width = w;
  if (cv.height !== h) cv.height = h;
  const g = cv.getContext('2d');
  g.clearRect(0, 0, w, h);
  g.fillStyle = '#2e2a24';
  for (let b = 1; b < 4; b++) g.fillRect(Math.round(w * b / 4), 0, 1, h);     // the beats
  if (!audio) return;
  const d = audio.mono, sr = audio.buf.sampleRate;
  const a = measureStart(i) * sr, per = barSec() * sr / w, step = Math.max(1, Math.floor(per / 48));
  g.fillStyle = c.tint || '#8d867a';
  for (let x = 0; x < w; x++) {
    let lo = 0, hi = 0;
    const from = Math.floor(a + x * per), to = Math.floor(a + (x + 1) * per);
    for (let s = Math.max(0, from); s < to && s < d.length; s += step) { const v = d[s]; if (v < lo) lo = v; else if (v > hi) hi = v; }
    const y0 = (0.5 - hi * 0.5) * h, y1 = (0.5 - lo * 0.5) * h;
    g.fillRect(x, y0, 1, Math.max(1, y1 - y0));
  }
}

// the whole track on one line: its level, and the sections coloured by pattern
function drawOverview() {
  const cv = $('overview'), dpr = Math.min(2, window.devicePixelRatio || 1);
  const w = Math.max(100, Math.round(cv.clientWidth * dpr)), h = Math.round(56 * dpr);
  if (cv.width !== w) cv.width = w;
  if (cv.height !== h) cv.height = h;
  const g = cv.getContext('2d');
  g.clearRect(0, 0, w, h);
  const dur = totalDuration();
  if (!dur) return;
  const band = Math.round(10 * dpr);
  for (const r of built.runs) {
    if (!r.pat) continue;
    const x0 = measureStart(r.start) / dur * w, x1 = measureStart(r.start + r.len) / dur * w;
    g.fillStyle = patColor(r.pat);
    g.fillRect(x0, h - band, Math.max(1, x1 - x0 - 1), band);
  }
  if (audio) {
    if (!audio.peaks || audio.peaks.length !== w) {
      const d = audio.mono, per = d.length / w, pk = new Float32Array(w);
      for (let x = 0; x < w; x++) {
        let m = 0;
        for (let s = Math.floor(x * per), e = Math.floor((x + 1) * per), st = Math.max(1, Math.floor(per / 200)); s < e; s += st) { const v = Math.abs(d[s]); if (v > m) m = v; }
        pk[x] = m;
      }
      audio.peaks = pk;
    }
    g.fillStyle = '#8d867a';
    const hh = h - band - 2;
    for (let x = 0; x < w; x++) { const v = audio.peaks[x] * hh; g.fillRect(x, (hh - v) / 2, 1, Math.max(1, v)); }
  }
  const r = selRange();
  if (r) {
    g.fillStyle = 'rgba(242,230,208,.22)';
    const x0 = measureStart(r[0]) / dur * w, x1 = measureStart(r[1] + 1) / dur * w;
    g.fillRect(x0, 0, Math.max(2, x1 - x0), h - band);
  }
  g.fillStyle = '#ffb000';
  g.fillRect(Math.round(position() / dur * w), 0, Math.max(1, dpr), h);
}
const totalDuration = () => audio ? audio.duration : (doc.measures.length ? measureStart(doc.measures.length) : 0);

// ---------- selection ----------
function select(a, b, opts = {}) {
  const n = doc.measures.length;
  if (!n) return;
  a = Math.max(0, Math.min(n - 1, a)); b = Math.max(0, Math.min(n - 1, b));
  sel = { a, b };
  for (let i = 0; i < cells.length; i++) paintCell(i);
  paintInspector(); paintSong(); drawOverview();
  if (opts.seek !== false) seek(measureStart(Math.min(a, b)));
  if (opts.scroll) cells[b].el.scrollIntoView({ block: 'nearest' });
  if (loopOn() && playing) restartAt(measureStart(Math.min(a, b)));
}

function columns() {
  if (cells.length < 2) return 1;
  const top = cells[0].el.offsetTop;
  let n = 1;
  while (n < cells.length && cells[n].el.offsetTop === top) n++;
  return n;
}

// ---------- editing the selection ----------
function eachSelected(fn) {
  const r = selRange();
  if (!r) return false;
  snapshot();
  for (let i = r[0]; i <= r[1]; i++) fn(doc.measures[i], i);
  changed();
  return true;
}
function selPattern() {            // the pattern all selected measures share, 0 = none, -1 = mixed
  const r = selRange();
  if (!r) return -1;
  const p = doc.measures[r[0]].pat;
  for (let i = r[0]; i <= r[1]; i++) if (doc.measures[i].pat !== p) return -1;
  return p;
}
function setPattern(p, picked) {
  const r = selRange();
  if (!r) return;
  // one measure, and a two-bar pattern picked by name: it takes the next one too, if that is free
  // (not while stepping through the numbers with [ ], which would grab it in passing)
  if (picked && p && r[0] === r[1] && patBars(doc, p) === 2 && r[0] + 1 < doc.measures.length && !doc.measures[r[0] + 1].pat) sel = { a: r[0], b: r[0] + 1 };
  eachSelected(m => { m.pat = p; if (!p) m.mute = 0; });
}
function stepPattern(dir) {
  const r = selRange();
  if (!r) return;
  const cur = doc.measures[r[0]].pat;
  setPattern(Math.max(0, Math.min(NUM_PATTERNS, cur + dir)));
}
function toggleMute(bit) {
  const r = selRange();
  if (!r) return;
  let all = true, any = false;
  for (let i = r[0]; i <= r[1]; i++) { if (!doc.measures[i].pat) continue; any = true; if (!(doc.measures[i].mute & (1 << bit))) all = false; }
  if (!any) { say('Give the measure a pattern first.'); return; }
  eachSelected(m => { if (!m.pat) return; if (all) m.mute &= ~(1 << bit); else m.mute |= (1 << bit); });
}
function setBars(n) {
  const p = selPattern();
  if (p <= 0) return;
  snapshot();
  doc.patterns[p] = Object.assign({ name: '' }, doc.patterns[p], { bars: n });
  changed();
}

// ---------- inspector ----------
function buildInspector() {
  const pats = $('pats');
  for (let p = 0; p <= NUM_PATTERNS; p++) {
    const b = document.createElement('button');
    b.textContent = p ? String(p) : 'none';
    b.dataset.p = p;
    if (p) b.style.setProperty('--c', patColor(p));
    b.addEventListener('click', () => setPattern(p, true));
    pats.appendChild(b);
  }
  const parts = $('parts');
  ROWS.forEach((name, b) => {
    const el = document.createElement('button');
    el.textContent = name;
    el.title = `${name} (key ${ROW_KEYS[b]})`;
    el.style.setProperty('--pc', `var(${PART_COLOR[b]})`);
    el.addEventListener('click', () => toggleMute(b));
    parts.appendChild(el);
  });
  $('bars1').addEventListener('click', () => setBars(1));
  $('bars2').addEventListener('click', () => setBars(2));
  $('patName').addEventListener('change', e => {
    const p = selPattern();
    if (p <= 0) return;
    snapshot();
    doc.patterns[p] = Object.assign({ bars: 1 }, doc.patterns[p], { name: e.target.value.trim() });
    changed();
  });
  $('allOn').addEventListener('click', () => eachSelected(m => { m.mute = 0; }));
  $('allOff').addEventListener('click', () => eachSelected(m => { if (m.pat) m.mute = MUTE_MASK; }));
  $('asPrev').addEventListener('click', () => {
    const r = selRange();
    if (!r || !r[0]) return;
    const src = doc.measures[r[0] - 1];
    eachSelected(m => { m.pat = src.pat; m.mute = src.mute; });
  });
  $('selRun').addEventListener('click', () => {
    const r = selRange();
    if (!r) return;
    const pl = place(built.runs, r[0]);
    if (pl) select(pl.run.start, pl.run.start + pl.run.len - 1, { seek: false });
  });
}

function paintInspector() {
  const r = selRange(), box = $('inspector');
  box.classList.toggle('idle', !r);
  const p = selPattern();
  $('selTitle').textContent = !r ? (doc.measures.length ? 'Click a measure' : 'Nothing selected')
    : r[0] === r[1] ? `Measure ${r[0] + 1}` : `Measures ${r[0] + 1}-${r[1] + 1} (${r[1] - r[0] + 1})`;
  const used = new Set(built.used);
  for (const b of $('pats').children) {
    const n = +b.dataset.p;
    b.className = (n === p && r ? 'cur ' : '') + (n && used.has(n) ? 'used ' : '') + (n && patBars(doc, n) === 2 ? 'two' : '');
  }
  const has = p > 0;
  $('patField').style.display = has ? '' : 'none';
  if (has) {
    const bars = patBars(doc, p);
    let uses = 0;
    for (const m of doc.measures) if (m.pat === p) uses++;
    $('patLab').innerHTML = `Pattern ${p} <span class="dim">everywhere it is used</span>`;
    $('bars1').className = bars === 1 ? 'cur' : '';
    $('bars2').className = bars === 2 ? 'cur' : '';
    if (document.activeElement !== $('patName')) $('patName').value = (doc.patterns[p] && doc.patterns[p].name) || '';
    $('patNote').textContent = `${uses} measure${uses === 1 ? '' : 's'} in this track. A 2-bar pattern takes two measures each time it plays.`;
  }
  let on = 0, off = 0, n = 0;
  const partBtns = $('parts').children;
  for (let b = 0; b < ROWS.length; b++) {
    on = off = 0;
    if (r) for (let i = r[0]; i <= r[1]; i++) { const m = doc.measures[i]; if (!m.pat) continue; if (m.mute & (1 << b)) off++; else on++; }
    n = on + off;
    partBtns[b].className = !n ? 'off' : off && on ? 'mixed' : off ? 'off' : '';
    partBtns[b].setAttribute('aria-pressed', String(!off && n > 0));
  }
}

// ---------- the song ----------
function paintSong() {
  const ul = $('issues'), ol = $('song');
  ul.textContent = ''; ol.textContent = '';
  for (const it of built.issues) {
    const li = document.createElement('li');
    li.textContent = it.text;
    li.addEventListener('click', () => select(it.at, it.at + it.len - 1, { scroll: true }));
    ul.appendChild(li);
  }
  const r = selRange();
  built.slots.slice(0, SONG_LENGTH).forEach((s, k) => {
    const li = document.createElement('li');
    const len = s.reps * patBars(doc, s.pat);
    if (r && r[0] < s.at + len && r[1] >= s.at) li.className = 'cur';
    li.style.setProperty('--c', patColor(s.pat));
    const muted = muteNames(s.mute);
    li.innerHTML = `<span class="n">${k + 1}</span><span class="p">P${s.pat}</span><span>x${s.reps}</span><span class="mu">${muted.length === ROWS.length ? 'all muted' : muted.length ? 'mute ' + muted.join(' ') : ''}</span>`;
    li.title = `Measures ${s.at + 1}-${s.at + len}`;
    li.addEventListener('click', () => select(s.at, s.at + len - 1, { scroll: true }));
    ol.appendChild(li);
  });
  if (!built.slots.length) ol.innerHTML = '<li class="emptySong">Nothing yet: give some measures a pattern.</li>';
  const n = Math.min(built.slots.length, SONG_LENGTH);
  $('songCount').textContent = built.slots.length ? `${n} / ${SONG_LENGTH} slots, ${built.used.length} / ${NUM_PATTERNS} patterns` : '';
}

// ---------- playback ----------
let ctx = null, src = null, playing = false, startedAt = 0, startedFrom = 0, cursor = 0, nowCell = -1;
let clickUntil = 0, loopA = 0, loopB = 0;
const loopOn = () => $('loop').checked && !!sel;

function position() {
  if (!playing) return cursor;
  let t = startedFrom + (ctx.currentTime - startedAt);
  if (loopB > loopA) t = loopA + ((t - loopA) % (loopB - loopA) + (loopB - loopA)) % (loopB - loopA);
  return t;
}
function stop() {
  if (src) { try { src.onended = null; src.stop(); } catch { /* already over */ } src = null; }
  if (playing) cursor = Math.min(position(), totalDuration());
  playing = false;
  $('play').textContent = 'Play';
}
function restartAt(t) {
  if (!audio) { cursor = t; return; }
  stop();
  ctx = ctx || new (window.AudioContext || window.webkitAudioContext)();
  if (ctx.state === 'suspended') ctx.resume();
  const r = selRange();
  loopA = loopB = 0;
  if (loopOn() && r) {
    loopA = Math.max(0, measureStart(r[0])); loopB = Math.min(audio.duration, measureStart(r[1] + 1));
    if (t < loopA || t >= loopB) t = loopA;
  }
  t = Math.max(0, Math.min(audio.duration - 0.01, t));
  src = ctx.createBufferSource();
  src.buffer = audio.buf;
  if (loopB > loopA) { src.loop = true; src.loopStart = loopA; src.loopEnd = loopB; }
  src.connect(ctx.destination);
  startedAt = ctx.currentTime + 0.03; startedFrom = t;
  src.start(startedAt, t);
  src.onended = () => { if (playing) { stop(); cursor = 0; } };
  playing = true; clickUntil = 0;
  $('play').textContent = 'Stop';
}
function seek(t) {
  cursor = Math.max(0, t);
  if (playing) restartAt(cursor);
}
function togglePlay() {
  if (!audio) { say('Open the track first to hear it.'); return; }
  if (playing) stop(); else restartAt(cursor);
}

// a tick on every beat, higher on the one: the way to check the bar lines by ear
function scheduleClicks() {
  if (!playing || !$('click').checked || loopB > loopA && loopB - loopA < 0.2) return;
  const now = ctx.currentTime, ahead = now + 0.25;
  let from = Math.max(clickUntil, now);
  // track time of `from`, ignoring the loop wrap (clicks follow the track position)
  const pos = t => {
    let p = startedFrom + (t - startedAt);
    if (loopB > loopA) p = loopA + ((p - loopA) % (loopB - loopA) + (loopB - loopA)) % (loopB - loopA);
    return p;
  };
  const beat = beatSec();
  let p = pos(from), k = Math.ceil((p - doc.downbeat) / beat - 1e-6);
  for (;;) {
    const at = from + (doc.downbeat + k * beat - p);
    if (at >= ahead) break;
    if (at >= now) {
      const o = ctx.createOscillator(), g = ctx.createGain();
      o.frequency.value = ((k % 4) + 4) % 4 === 0 ? 1760 : 880;
      g.gain.setValueAtTime(0.25, at); g.gain.exponentialRampToValueAtTime(0.001, at + 0.05);
      o.connect(g).connect(ctx.destination);
      o.start(at); o.stop(at + 0.06);
    }
    k++;
    if (loopB > loopA && doc.downbeat + k * beat >= loopB) break;      // the next pass schedules after the wrap
  }
  clickUntil = ahead;
}

function tick() {
  requestAnimationFrame(tick);
  const t = position();
  const mm = Math.floor(t / 60), ss = t - mm * 60;
  const txt = `${mm}:${ss < 10 ? '0' : ''}${ss.toFixed(1)}`;
  if ($('clock').textContent !== txt) $('clock').textContent = txt;
  const i = doc.measures.length ? measureAt(t) : -1;
  if (i !== nowCell) { const old = nowCell; nowCell = i; paintCell(old); paintCell(i); }
  if (cells[i]) cells[i].head.style.left = `${Math.max(0, Math.min(1, (t - measureStart(i)) / barSec())) * 100}%`;
  if (playing) { scheduleClicks(); drawOverview(); }
}

// ---------- files ----------
let sayTimer = 0;
function say(msg, bad) {
  const n = $('notice');
  n.textContent = msg; n.hidden = !msg; n.className = 'notice' + (bad ? ' bad' : '');
  clearTimeout(sayTimer);
  if (msg && !bad) sayTimer = setTimeout(() => { n.hidden = true; }, 9000);
}

async function loadAudio(file, forceDetect) {
  say(`Reading ${file.name}...`);
  stop();
  try {
    ctx = ctx || new (window.AudioContext || window.webkitAudioContext)();
    const buf = await ctx.decodeAudioData(await file.arrayBuffer());
    audio = { buf, mono: monoMix(buf), duration: buf.duration, name: file.name };
  } catch (e) {
    say(`This browser could not decode ${file.name}. Try MP3 or WAV.`, true);
    return;
  }
  const marked = doc.measures.some(m => m.pat);
  if (!doc.name) { doc.name = file.name.replace(/\.[^.]+$/, ''); $('name').value = doc.name; }
  if (marked && !forceDetect) {
    // an arrangement is open: its bar lines stand, the audio is laid under them
    say(doc.audio && doc.audio !== file.name
      ? `Kept the arrangement and its bar lines (made for ${doc.audio}). Press New first to start over with this track.`
      : 'Track loaded under the arrangement. Bar lines are the saved ones.');
    doc.audio = file.name;
    cursor = 0; layout();
    return;
  }
  doc.audio = file.name;
  await detect();
}

async function detect() {
  if (!audio) return;
  say('Finding the bar lines...');
  await new Promise(r => setTimeout(r, 30));            // let the message paint
  const r = analyse(audio.mono, audio.buf.sampleRate);
  snapshot();
  doc.bpm = r.bpm; doc.downbeat = r.downbeat;
  cursor = 0; sel = null;
  layout();
  if (r.confidence < 0.25) {
    say(`Bar lines are a guess: ${r.bpm.toFixed(2)} BPM, but this track does not hold a steady tempo (a live drummer?), so they will slide. Check with the click, under Bar lines.`, true);
    $('tempoBox').open = true;
  } else {
    say(`${r.bpm.toFixed(2)} BPM, ${doc.measures.length} measures. ${r.barConfidence < 0.3 ? 'Not sure which beat is the one: check with the click, under Bar lines.' : 'Check the bar lines with the click before you start.'}`);
    if (r.barConfidence < 0.3) $('tempoBox').open = true;
  }
}

async function loadArr(file) {
  try {
    const d = parse(await file.text());
    undo = [];
    doc = d; sel = null; cursor = 0;
    if (audio && d.audio && d.audio !== audio.name) { stop(); audio = null; }
    $('name').value = doc.name;
    layout();
    say(audio ? 'Arrangement loaded.' : `Arrangement loaded. Open the track${doc.audio ? ` (${doc.audio})` : ''} to see and hear it.`);
  } catch (e) {
    say(e.message, true);
  }
}

function fileName() {
  const base = (doc.name || 'song').replace(/[^A-Za-z0-9 _-]+/g, '').trim().replace(/\s+/g, '-').slice(0, 40) || 'song';
  return base + '.arr';          // FILTRE lists names up to 63 characters
}
function download() {
  if (!doc.measures.some(m => m.pat)) { say('Nothing to save yet: give some measures a pattern.'); return; }
  const blob = new Blob([serialize(doc)], { type: 'text/plain' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob); a.download = fileName();
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 2000);
  const n = built.issues.length;
  say(`Saved ${fileName()}: ${Math.min(built.slots.length, SONG_LENGTH)} song slots.${n ? ` ${n} thing${n > 1 ? 's' : ''} to fix first, listed beside the song.` : ' Copy it to /groovebox/songs/ on the card.'}`, n > 0);
}

function pickFile(file) {
  if (!file) return;
  if (/\.arr$/i.test(file.name) || file.type === 'text/plain') loadArr(file); else loadAudio(file);
}

// ---------- bar-line controls ----------
function gridAction(g) {
  if (g === 'detect') { if (audio) { if (!doc.measures.some(m => m.pat) || confirm('Detect the bar lines again? Your patterns stay on their measure numbers, so they may no longer sit on the same music.')) detect(); } else say('Open the track first.'); return; }
  if (g === 'add8') { snapshot(); for (let i = 0; i < 8; i++) doc.measures.push({ pat: 0, mute: 0 }); layout(); return; }
  if (!doc.measures.length) return;
  snapshot();
  const bar = barSec(), beat = beatSec();
  if (g === 'half') doc.bpm /= 2;
  else if (g === 'double') doc.bpm *= 2;
  else if (g === 'beat-' || g === 'beat+') doc.downbeat += g === 'beat+' ? beat : -beat;
  else if (g.startsWith('ms')) doc.downbeat += parseInt(g.slice(2), 10) / 1000;
  else if (g === 'here') { const t = position(); doc.downbeat = t - Math.round((t - doc.downbeat) / bar) * bar; }
  else if (g === 'bar+') { doc.downbeat += bar; doc.measures.shift(); if (sel) { sel.a = Math.max(0, sel.a - 1); sel.b = Math.max(0, sel.b - 1); } }
  else if (g === 'bar-') {
    if (doc.downbeat - bar < -0.05) { undo.pop(); say('Measure 1 is already the first bar of the file.'); return; }
    doc.downbeat -= bar; doc.measures.unshift({ pat: 0, mute: 0 });
    if (sel) { sel.a++; sel.b++; }
  }
  // the grid is only shifted within a bar here: keep measure 1 inside the file
  while (doc.downbeat < -0.05) doc.downbeat += barSec();
  if (doc.downbeat < 0) doc.downbeat = 0;
  doc.bpm = Math.max(40, Math.min(300, doc.bpm));
  layout();
}

// ---------- wiring ----------
function wire() {
  $('openAudio').addEventListener('click', () => $('fileAudio').click());
  $('openArr').addEventListener('click', () => $('fileArr').click());
  $('fileAudio').addEventListener('change', e => { pickFile(e.target.files[0]); e.target.value = ''; });
  $('fileArr').addEventListener('change', e => { if (e.target.files[0]) loadArr(e.target.files[0]); e.target.value = ''; });
  $('saveArr').addEventListener('click', download);
  $('newDoc').addEventListener('click', () => {
    if (doc.measures.some(m => m.pat) && !confirm('Start over? The arrangement on screen is dropped (save it first if you want it).')) return;
    stop(); doc = newDoc(); audio = null; sel = null; undo = []; cursor = 0;
    $('name').value = ''; layout(); say('');
  });
  $('name').addEventListener('input', e => { doc.name = e.target.value; save(); });
  $('play').addEventListener('click', togglePlay);
  $('loop').addEventListener('change', () => { if (playing) restartAt(position()); });
  $('bpm').addEventListener('change', e => {
    const v = parseFloat(e.target.value);
    if (!(v >= 40 && v <= 300)) { e.target.value = doc.bpm; return; }
    snapshot(); doc.bpm = v; layout();
  });
  document.querySelectorAll('[data-g]').forEach(b => b.addEventListener('click', () => gridAction(b.dataset.g)));

  // measures: click, shift-click, drag
  const grid = $('grid');
  let dragging = false;
  const cellOf = e => { const el = e.target.closest && e.target.closest('.m'); return el ? +el.dataset.i : -1; };
  grid.addEventListener('pointerdown', e => {
    const i = cellOf(e);
    if (i < 0) return;
    if (e.shiftKey && sel) select(sel.a, i, { seek: false });
    else { select(i, i); dragging = e.pointerType === 'mouse'; }
  });
  grid.addEventListener('pointermove', e => {
    if (!dragging || !sel) return;
    const el = document.elementFromPoint(e.clientX, e.clientY), m = el && el.closest('.m');
    if (m && +m.dataset.i !== sel.b) select(sel.a, +m.dataset.i, { seek: false });
  });
  window.addEventListener('pointerup', () => { dragging = false; });

  $('overview').addEventListener('click', e => {
    const r = e.currentTarget.getBoundingClientRect(), t = (e.clientX - r.left) / r.width * totalDuration();
    if (!doc.measures.length) return;
    const i = Math.max(0, Math.min(doc.measures.length - 1, measureAt(t)));
    select(i, i, { seek: false, scroll: true });
    seek(t);
  });

  window.addEventListener('keydown', e => {
    const tag = e.target.tagName;
    if ((tag === 'INPUT' && e.target.type !== 'checkbox') || tag === 'TEXTAREA') { if (e.key === 'Enter' || e.key === 'Escape') e.target.blur(); return; }
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z') { e.preventDefault(); undoLast(); return; }
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') { e.preventDefault(); download(); return; }
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    const k = e.key;
    if (k === ' ') { e.preventDefault(); togglePlay(); return; }
    if (k === 'l' || k === 'L') { $('loop').checked = !$('loop').checked; if (playing) restartAt(position()); return; }
    if (k === 'k' || k === 'K') { $('click').checked = !$('click').checked; return; }
    if (!doc.measures.length) return;
    const cols = columns();
    const move = d => {
      e.preventDefault();
      if (!sel) { select(0, 0, { scroll: true }); return; }
      if (e.shiftKey) select(sel.a, sel.b + d, { seek: false, scroll: true });
      else { const r = selRange(), to = (d > 0 ? r[1] : r[0]) + d; select(to, to, { scroll: true }); }
    };
    if (k === 'ArrowRight') return move(1);
    if (k === 'ArrowLeft') return move(-1);
    if (k === 'ArrowDown') return move(cols);
    if (k === 'ArrowUp') return move(-cols);
    if (k === 'Enter' && sel) { restartAt(measureStart(selRange()[0])); return; }
    if (k === 'Escape') { sel = null; changed(); return; }
    if (k === '[') return stepPattern(-1);
    if (k === ']') return stepPattern(1);
    if (k === 'Delete' || k === 'Backspace') { e.preventDefault(); return setPattern(0); }
    const row = ROW_KEYS.indexOf(k);
    if (row >= 0) { e.preventDefault(); toggleMute(row); }
  });

  // drop a file anywhere
  let depth = 0;
  window.addEventListener('dragenter', e => { e.preventDefault(); depth++; document.body.classList.add('drop'); });
  window.addEventListener('dragleave', () => { if (--depth <= 0) { depth = 0; document.body.classList.remove('drop'); } });
  window.addEventListener('dragover', e => e.preventDefault());
  window.addEventListener('drop', e => {
    e.preventDefault(); depth = 0; document.body.classList.remove('drop');
    pickFile(e.dataTransfer.files[0]);
  });
  window.addEventListener('resize', () => { queueWaves(); drawOverview(); });
}

// ---------- start ----------
buildInspector();
wire();
try {
  const kept = localStorage.getItem(STORE);
  if (kept) {
    doc = parse(kept);
    $('name').value = doc.name;
    if (doc.measures.some(m => m.pat)) say(`Picked up where you left off. Open the track${doc.audio ? ` (${doc.audio})` : ''} to see and hear it.`);
    else doc = newDoc();
  }
} catch { doc = newDoc(); }
layout();
requestAnimationFrame(tick);

// for tools/arranger-test/ui.test.mjs
window.__arranger = { get doc() { return doc; }, get built() { return built; }, select, setPattern, toggleMute, setBars, serialize: () => serialize(doc) };
