// FILTRE arranger - beat.js
// Finds the tempo and the bar lines of a track that keeps a steady tempo in
// 4/4 (drum machines, sample loops). No DOM here: it takes samples and
// returns numbers, so tools/arranger-test can run it under node.
//
//   analyse(mono Float32Array, sampleRate) ->
//     { bpm, downbeat, firstSound, duration, confidence }
//   downbeat = seconds where measure 1 starts (the first bar line that is
//   no more than a beat before the first sound).

const TARGET_RATE = 11025;      // plenty for onsets, and 4x less work
const FFT_N = 512;
const BANDS = 20;
const ONSET_LAG = 0.012;        // measured against click tracks (tools/arranger-test)

function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k, b = a + len / 2;
        const xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
        re[b] = re[a] - xr; im[b] = im[a] - xi;
        re[a] += xr; im[a] += xi;
        const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
      }
    }
  }
}

// Onset strength, one value per frame (~100 per second), plus a coarse
// spectrum per frame for telling bars apart.
export function features(mono, sampleRate) {
  const dec = Math.max(1, Math.round(sampleRate / TARGET_RATE));
  const rate = sampleRate / dec;
  const n = Math.floor(mono.length / dec);
  const x = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    let s = 0;
    for (let k = 0; k < dec; k++) s += mono[i * dec + k];
    x[i] = s / dec;
  }
  const hop = Math.max(1, Math.round(rate / 100));
  const fps = rate / hop;
  const frames = Math.max(0, Math.floor((n - FFT_N) / hop) + 1);
  const win = new Float32Array(FFT_N);
  for (let i = 0; i < FFT_N; i++) win[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (FFT_N - 1));

  // band edges: roughly logarithmic from 40 Hz to the top
  const half = FFT_N / 2, edges = [];
  for (let b = 0; b <= BANDS; b++) {
    const hz = 40 * Math.pow((rate / 2) / 40, b / BANDS);
    edges.push(Math.min(half, Math.max(1, Math.round(hz / (rate / FFT_N)))));
  }
  for (let b = 1; b <= BANDS; b++) if (edges[b] <= edges[b - 1]) edges[b] = Math.min(half, edges[b - 1] + 1);
  const lowBin = Math.max(2, Math.round(150 / (rate / FFT_N)));

  const flux = new Float32Array(frames), low = new Float32Array(frames), level = new Float32Array(frames);
  const bands = new Float32Array(frames * BANDS);
  const re = new Float32Array(FFT_N), im = new Float32Array(FFT_N);
  let prev = new Float32Array(half), cur = new Float32Array(half);
  for (let f = 0; f < frames; f++) {
    const o = f * hop;
    let e = 0;
    for (let i = 0; i < FFT_N; i++) { const v = x[o + i]; re[i] = v * win[i]; im[i] = 0; e += v * v; }
    level[f] = Math.sqrt(e / FFT_N);
    fft(re, im);
    let fl = 0, lo = 0;
    for (let k = 1; k < half; k++) {
      const m = Math.log1p(100 * Math.hypot(re[k], im[k]));
      cur[k] = m;
      const d = m - prev[k];
      if (d > 0) { fl += d; if (k <= lowBin) lo += d; }
    }
    flux[f] = fl; low[f] = lo;
    for (let b = 0; b < BANDS; b++) {
      let s = 0;
      for (let k = edges[b]; k < edges[b + 1]; k++) s += cur[k];
      bands[f * BANDS + b] = s / Math.max(1, edges[b + 1] - edges[b]);
    }
    const t = prev; prev = cur; cur = t;
  }
  // a frame is stamped with the middle of its window
  return { flux, low, level, bands, fps, frames, t0: (FFT_N / 2) / rate, duration: mono.length / sampleRate };
}

// subtract the local average, keep what sticks out
function sharpen(env, fps) {
  const n = env.length, out = new Float32Array(n), w = Math.max(1, Math.round(fps * 0.4));
  let sum = 0;
  const pre = new Float64Array(n + 1);
  for (let i = 0; i < n; i++) { sum += env[i]; pre[i + 1] = sum; }
  for (let i = 0; i < n; i++) {
    const a = Math.max(0, i - w), b = Math.min(n, i + w + 1);
    const v = env[i] - (pre[b] - pre[a]) / (b - a);
    out[i] = v > 0 ? v : 0;
  }
  return out;
}

// How well onsets line up when time is wrapped at `period` frames:
// the strongest phase against the average phase.
function foldScore(env, period, scratch, count) {
  const bins = Math.max(8, Math.min(scratch.length, Math.round(period)));   // one bin per frame: none left empty
  scratch.fill(0); count.fill(0);
  const k = bins / period;
  for (let i = 0; i < env.length; i++) {
    const b = Math.floor((i % period) * k) % bins;     // i % period works for a fractional period
    scratch[b] += env[i]; count[b]++;
  }
  let best = 0, at = 0, total = 0;
  for (let b = 0; b < bins; b++) {
    const m = count[b] ? scratch[b] / count[b] : 0;
    total += m;
    if (m > best) { best = m; at = b; }
  }
  const mean = total / bins;
  return { score: mean > 0 ? best / mean : 0, phase: (at + 0.5) / bins };
}

function interp(env, pos) {
  const i = Math.floor(pos);
  if (i < 0 || i + 1 >= env.length) return 0;
  const f = pos - i;
  return env[i] * (1 - f) + env[i + 1] * f;
}

// sum of the envelope on a grid of beats
function gridSum(env, period, phase) {
  let s = 0, n = 0;
  for (let p = phase; p < env.length - 1; p += period) { s += interp(env, p); n++; }
  return n ? s / n : 0;
}

export function analyse(mono, sampleRate, opts = {}) {
  const lo = opts.minBpm || 70, hi = opts.maxBpm || 180;
  const ft = features(mono, sampleRate);
  const { fps, frames } = ft;
  if (frames < fps * 4) return { bpm: 120, downbeat: 0, firstSound: 0, duration: ft.duration, confidence: 0 };

  // kicks count double: they are what the grid has to sit on
  const raw = new Float32Array(frames);
  for (let i = 0; i < frames; i++) raw[i] = ft.flux[i] + 2 * ft.low[i];
  const env = sharpen(raw, fps);

  // --- tempo, roughly: how well the onsets repeat after 1, 2 .. 8 beats.
  // (Wrapping the whole track at a candidate tempo only works once the tempo
  // is nearly right: a 0.1 BPM error drifts a full beat in five minutes.) ---
  const maxLag = Math.min(frames - 1, Math.ceil(fps * 60 / lo * 8) + 2);
  const acf = new Float32Array(maxLag + 1);
  for (let lag = 1; lag <= maxLag; lag++) {
    let s = 0;
    for (let i = lag; i < frames; i++) s += env[i] * env[i - lag];
    acf[lag] = s / (frames - lag);
  }
  let rough = 120, roughScore = -1;
  for (let bpm = lo; bpm <= hi + 1e-9; bpm += 0.05) {
    const p = fps * 60 / bpm;
    let s = 0;
    for (let m = 1; m <= 8; m++) s += interp(acf, m * p);
    // most music people sample sits near 120: break octave ties that way
    s *= 0.6 + 0.4 * Math.exp(-0.5 * Math.pow(Math.log2(bpm / 120) / 0.7, 2));
    if (s > roughScore) { roughScore = s; rough = bpm; }
  }

  // --- then exactly: wrap the track at the tempo and look for the sharpest
  // pile-up, in steps that drift half a frame over the whole track ---
  const scratch = new Float64Array(256), count = new Uint32Array(256);
  const step = Math.max(0.0005, 0.5 * rough / frames);
  let t = { bpm: rough, score: -1 };
  for (let bpm = rough - 0.6; bpm <= rough + 0.6; bpm += step) {
    const r = foldScore(env, fps * 60 / bpm, scratch, count);
    if (r.score > t.score) t = { bpm, score: r.score };
  }
  let bpm = t.bpm, period = fps * 60 / bpm, phase = 0, top = -1;
  for (let d = -step; d <= step + 1e-12; d += step / 4) {
    const p = fps * 60 / (t.bpm + d);
    for (let ph = 0; ph < p; ph += 0.5) {
      const s = gridSum(env, p, ph);
      if (s > top) { top = s; bpm = t.bpm + d; period = p; phase = ph; }
    }
  }
  for (let ph = phase - 0.5; ph <= phase + 0.5; ph += 0.1) {      // 1 ms steps
    const q = ((ph % period) + period) % period, s = gridSum(env, period, q);
    if (s > top) { top = s; phase = q; }
  }

  // --- which beat is the "one"? Things change on bar lines: compare the
  // four beats before each beat with the four after it. ---
  const nBeats = Math.floor((frames - 1 - phase) / period);
  const spec = new Float32Array(Math.max(0, nBeats) * BANDS), kick = new Float32Array(Math.max(0, nBeats));
  for (let b = 0; b < nBeats; b++) {
    const a = Math.round(phase + b * period), z = Math.min(frames, Math.round(phase + (b + 1) * period));
    for (let f = a; f < z; f++)
      for (let k = 0; k < BANDS; k++) spec[b * BANDS + k] += ft.bands[f * BANDS + k];
    for (let k = 0; k < BANDS; k++) spec[b * BANDS + k] /= Math.max(1, z - a);
    kick[b] = interp(ft.low, phase + b * period) + interp(ft.low, phase + b * period + 1);
  }
  const nov = [0, 0, 0, 0], kk = [0, 0, 0, 0], cnt = [0, 0, 0, 0];
  for (let b = 4; b + 4 <= nBeats; b++) {
    // not normalised per bar: a loud change (a part coming in) should count
    // for more than a wobble in a quiet passage
    let dist = 0;
    for (let k = 0; k < BANDS; k++) {
      let p = 0, q = 0;
      for (let j = 0; j < 4; j++) { p += spec[(b - 4 + j) * BANDS + k]; q += spec[(b + j) * BANDS + k]; }
      dist += (p - q) * (p - q);
    }
    nov[b % 4] += dist * dist;                  // squared again: a few big changes outweigh a steady wobble
    kk[b % 4] += kick[b]; cnt[b % 4]++;
  }
  let one = 0, bestOne = -1, sumNov = 0, sumK = 0;
  for (let j = 0; j < 4; j++) { nov[j] /= Math.max(1, cnt[j]); kk[j] /= Math.max(1, cnt[j]); sumNov += nov[j]; sumK += kk[j]; }
  const score4 = [];
  for (let j = 0; j < 4; j++) {
    const s = (sumNov > 0 ? nov[j] / sumNov : 0.25) + 0.25 * (sumK > 0 ? kk[j] / sumK : 0.25);
    score4.push(s);
    if (s > bestOne) { bestOne = s; one = j; }
  }
  const sorted = score4.slice().sort((a, b) => b - a);

  // --- seconds ---
  const beatSec = 60 / bpm, barSec = beatSec * 4;
  let peakLevel = 0;
  for (let i = 0; i < frames; i++) if (ft.level[i] > peakLevel) peakLevel = ft.level[i];
  let first = 0;
  while (first < frames && ft.level[first] < peakLevel * 0.02) first++;
  const firstSound = Math.max(0, first / fps);
  // the flux peaks while the attack is still entering the window
  let downbeat = ft.t0 + (phase + one * period) / fps + ONSET_LAG;
  const limit = firstSound - beatSec;        // measure 1 may open with up to a beat of rest
  downbeat -= Math.ceil((downbeat - limit) / barSec) * barSec;
  if (downbeat < limit) downbeat += barSec;
  if (downbeat < 0) downbeat = downbeat > -0.05 ? 0 : downbeat + barSec;

  return {
    bpm: Math.round(bpm * 1000) / 1000, downbeat, firstSound, duration: ft.duration,
    confidence: Math.max(0, Math.min(1, (t.score - 1.5) / 4)),             // how sharp the pile-up was
    barConfidence: sorted[0] > 0 ? Math.max(0, Math.min(1, (sorted[0] - sorted[1]) / sorted[0] * 2)) : 0,
  };
}

// mono mix of an AudioBuffer-like { numberOfChannels, getChannelData }
export function monoMix(buf) {
  const n = buf.length, ch = buf.numberOfChannels, out = new Float32Array(n);
  for (let c = 0; c < ch; c++) {
    const d = buf.getChannelData(c);
    for (let i = 0; i < n; i++) out[i] += d[i];
  }
  if (ch > 1) for (let i = 0; i < n; i++) out[i] /= ch;
  return out;
}
