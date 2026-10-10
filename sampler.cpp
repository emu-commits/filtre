// ============================================================
// FILTRE - sampler.cpp
// ============================================================
#include "sampler.h"
#include "audio_engine.h"
#include <SD.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif

uint8_t*   g_arena     = nullptr;
uint32_t   g_arenaUsed = 0;
uint32_t   g_arenaCap  = 0;
SampleInfo g_samples[MAX_SAMPLES];
int16_t    g_ulawTable[256];
volatile bool g_tailHeld = false;
extern volatile uint32_t g_rsmpArmFrames;   // mic_sampler.cpp

static void cancelCapture() {            // call with the engine parked
    g_rsmpRemain = 0; g_rsmpWr = 0; g_rsmpArmFrames = 0;
    g_tailHeld = false;
}

// ---------- mu-law (G.711) ----------
static uint8_t ulawEncode(int32_t pcm) {
    const int32_t BIAS = 0x84, CLIP = 32635;
    uint8_t sign = 0;
    if (pcm < 0) { pcm = -pcm; sign = 0x80; }
    if (pcm > CLIP) pcm = CLIP;
    pcm += BIAS;
    int exp = 7;
    for (int32_t mask = 0x4000; (pcm & mask) == 0 && exp > 0; exp--, mask >>= 1) {}
    uint8_t mant = (uint8_t)((pcm >> (exp + 3)) & 0x0F);
    return (uint8_t)~(sign | (exp << 4) | mant);
}

static void ulawBuildTable() {
    for (int i = 0; i < 256; i++) {
        uint8_t u = (uint8_t)~i;
        int32_t s = (((int32_t)(u & 0x0F) << 3) + 0x84) << ((u >> 4) & 7);
        s -= 0x84;
        g_ulawTable[i] = (int16_t)((u & 0x80) ? -s : s);
    }
}

// ---------- arena ----------
bool samplerInit() {
    ulawBuildTable();
    uint32_t bytes = ARENA_MAX_BYTES;
#ifdef ARDUINO
    // Take what the heap can give while leaving ARENA_HEAP_RESERVE for the
    // rest of the firmware. There is no PSRAM on the Cardputer-ADV.
    uint32_t freeNow = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (freeNow > ARENA_HEAP_RESERVE && freeNow - ARENA_HEAP_RESERVE < bytes)
        bytes = freeNow - ARENA_HEAP_RESERVE;
    if (bytes > largest) bytes = largest;
    bytes &= ~1023u;
    while (bytes >= ARENA_MIN_BYTES) {
        g_arena = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (g_arena) break;
        bytes -= 8 * 1024;
    }
#else
    g_arena = (uint8_t*)malloc(bytes);
#endif
    if (!g_arena) { g_arenaCap = 0; return false; }
    g_arenaCap = bytes;
    samplerClearAll();
    return true;
}

void samplerEnsureDirs() {
    static const char* dirs[] = { DIR_ROOT, DIR_SAMPLES, DIR_WAVETABLES,
                                  DIR_PROJECTS, DIR_LOOPS, DIR_BOUNCE };
    for (auto d : dirs) if (!SD.exists(d)) SD.mkdir(d);
}

void samplerClearAll() {
    engineLock();
    engineStopSampleVoices();
    cancelCapture();
    g_arenaUsed = 0;
    memset(g_samples, 0, sizeof(g_samples));
    engineUnlock();
}

int samplerFindByName(const char* filename) {
    for (int i = 0; i < MAX_SAMPLES; i++)
        if (g_samples[i].used && g_samples[i].owner == OWN_ONESHOT &&
            strncmp(g_samples[i].name, filename, SAMPLE_NAME_LEN) == 0)
            return i;
    return -1;
}

static int freeSlot() {
    for (int i = 0; i < MAX_SAMPLES; i++) if (!g_samples[i].used) return i;
    return -1;
}

uint32_t samplerBytes(int slot) {
    if (!samplerValid(slot)) return 0;
    uint32_t b = g_samples[slot].length * (g_samples[slot].fmt == FMT_PCM16 ? 2 : 1);
    return (b + 1) & ~1u;
}

uint32_t samplerOwnerBytes(uint8_t owner) {
    uint32_t n = 0;
    for (int i = 0; i < MAX_SAMPLES; i++)
        if (g_samples[i].used && g_samples[i].owner == owner) n += samplerBytes(i);
    return n;
}

void samplerFree(int slot) {
    if (!samplerValid(slot)) return;
    engineLock();
    engineStopSampleVoices();            // voices hold raw arena pointers
    cancelCapture();                     // and a capture in the tail is about to be moved over
    uint32_t off = g_samples[slot].offset, sz = samplerBytes(slot);
    memmove(g_arena + off, g_arena + off + sz, g_arenaUsed - off - sz);
    for (int i = 0; i < MAX_SAMPLES; i++)
        if (g_samples[i].used && g_samples[i].offset > off) g_samples[i].offset -= sz;
    g_arenaUsed -= sz;
    g_samples[slot].used = false;
    engineUnlock();
}

static int registerSample(const char* name, uint32_t frames, uint32_t rate,
                          uint8_t fmt, uint8_t owner) {
    int slot = freeSlot();
    if (slot < 0) return -1;
    SampleInfo& s = g_samples[slot];
    strncpy(s.name, name, SAMPLE_NAME_LEN - 1);
    s.name[SAMPLE_NAME_LEN - 1] = 0;
    s.offset = g_arenaUsed;
    s.length = frames;
    s.rate   = rate;
    s.fmt    = fmt;
    s.owner  = owner;
    s.used   = true;
    g_arenaUsed += samplerBytes(slot);
    return slot;
}

int samplerAddRaw(const char* name, const uint8_t* data, uint32_t frames,
                  uint32_t rate, uint8_t fmt, uint8_t owner) {
    uint32_t bytes = frames * (fmt == FMT_PCM16 ? 2 : 1);
    if (g_tailHeld) return -1;
    if (!g_arena || ((bytes + 1) & ~1u) > samplerFreeBytes()) return -1;
    memcpy(g_arena + g_arenaUsed, data, bytes);
    return registerSample(name, frames, rate, fmt, owner);
}

int16_t* samplerScratch(uint32_t* maxFrames) {
    if (!g_arena) { *maxFrames = 0; return nullptr; }
    *maxFrames = (g_arenaCap - g_arenaUsed) / 2;
    return (int16_t*)(g_arena + g_arenaUsed);
}

int samplerCommitScratch(const char* name, uint32_t start, uint32_t frames,
                         uint32_t rate, uint8_t owner) {
    uint32_t maxF; int16_t* tail = samplerScratch(&maxF);
    if (!tail || start + frames > maxF || frames == 0) return -1;
    if (start) memmove(tail, tail + start, frames * 2);
    g_tailHeld = false;
    return registerSample(name, frames, rate, FMT_PCM16, owner);
}

// ---------- WAV parsing ----------
static uint32_t rdU32(File& f) {
    uint8_t b[4]; f.read(b, 4);
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static uint16_t rdU16(File& f) {
    uint8_t b[2]; f.read(b, 2);
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

bool WavReader::open(File& file) {
    f = &file;
    file.seek(0);
    char id[5] = {0};

    file.read((uint8_t*)id, 4);
    if (strncmp(id, "RIFF", 4) != 0) return false;
    rdU32(file);                           // riff size
    file.read((uint8_t*)id, 4);
    if (strncmp(id, "WAVE", 4) != 0) return false;

    uint16_t fmt = 0;
    uint32_t dataSize = 0;
    channels = bits = 0; rate = 0; dataPos = 0;

    // walk chunks
    while (file.available() >= 8) {
        file.read((uint8_t*)id, 4);
        uint32_t sz = rdU32(file);
        if (strncmp(id, "fmt ", 4) == 0) {
            uint32_t start = file.position();
            fmt      = rdU16(file);
            channels = rdU16(file);
            rate     = rdU32(file);
            rdU32(file); rdU16(file);      // byte rate, block align
            bits     = rdU16(file);
            if (fmt == 0xFFFE && sz >= 26) {   // WAVE_FORMAT_EXTENSIBLE: real tag in the GUID
                rdU16(file); rdU16(file); rdU32(file);
                fmt = rdU16(file);
            }
            file.seek(start + sz + (sz & 1));
        } else if (strncmp(id, "data", 4) == 0) {
            dataSize = sz;
            dataPos  = file.position();
            break;                         // samples follow; nothing after matters
        } else {
            file.seek(file.position() + sz + (sz & 1));
        }
    }

    if (dataPos == 0 || rate == 0) return false;
    if (channels < 1 || channels > 2) return false;
    isFloat = (fmt == 3);
    if (isFloat) { if (bits != 32) return false; }
    else if (fmt != 1 || (bits != 8 && bits != 16 && bits != 24)) return false;

    bytesPerFrame = channels * (bits / 8);
    uint32_t avail = (uint32_t)file.available();
    if (dataSize == 0 || dataSize > avail) dataSize = avail;   // streamed / damaged header
    frames = dataSize / bytesPerFrame;
    pos = 0;
    return frames > 0;
}

void WavReader::seekFrame(uint32_t frame) {
    if (frame > frames) frame = frames;
    pos = frame;
    f->seek(dataPos + frame * bytesPerFrame);
}

uint32_t WavReader::read(int16_t* dst, uint32_t maxFrames) {
    static uint8_t chunk[512];
    uint32_t perChunk = sizeof(chunk) / bytesPerFrame;
    uint32_t done = 0;
    if (maxFrames > frames - pos) maxFrames = frames - pos;

    while (done < maxFrames) {
        uint32_t n = maxFrames - done;
        if (n > perChunk) n = perChunk;
        int got = f->read(chunk, n * bytesPerFrame);
        if (got <= 0) break;
        uint32_t gotFrames = (uint32_t)got / bytesPerFrame;

        for (uint32_t i = 0; i < gotFrames; i++) {
            const uint8_t* p = chunk + i * bytesPerFrame;
            int32_t sum = 0;
            for (uint16_t c = 0; c < channels; c++) {
                int32_t v;
                if (isFloat) {
                    float fv; memcpy(&fv, p, 4);
                    if (fv > 1.0f) fv = 1.0f; else if (fv < -1.0f) fv = -1.0f;
                    v = (int32_t)(fv * 32767.0f);
                    p += 4;
                } else if (bits == 16) {
                    v = (int16_t)(p[0] | (p[1] << 8)); p += 2;
                } else if (bits == 24) {
                    v = (int16_t)(p[1] | (p[2] << 8)); p += 3;
                } else {                       // 8-bit unsigned
                    v = ((int32_t)p[0] - 128) << 8; p += 1;
                }
                sum += v;
            }
            dst[done + i] = (int16_t)(sum / channels);
        }
        done += gotFrames;
        if (gotFrames < n) break;
    }
    pos += done;
    return done;
}

bool wavDecodeToMono16(File& f, int16_t* dst, uint32_t maxFrames,
                       uint32_t& outFrames, uint32_t& outRate) {
    WavReader w;
    if (!w.open(f)) return false;
    outFrames = w.read(dst, maxFrames);
    outRate   = w.rate;
    return outFrames > 0;
}

// ---------- one-shots ----------
int samplerLoad(const char* filename) {
    if (strlen(filename) >= SAMPLE_NAME_LEN) return -1;     // lanes are saved by a 31-char name
    int existing = samplerFindByName(filename);
    if (existing >= 0) return existing;
    if (freeSlot() < 0 || !g_arena || g_tailHeld) return -1;

    char path[80];
    snprintf(path, sizeof(path), "%s/%s", DIR_SAMPLES, filename);
    File f = SD.open(path, FILE_READ);
    if (!f) return -1;

    uint32_t freeFrames; int16_t* dst = samplerScratch(&freeFrames);
    uint32_t frames = 0, rate = 0;
    bool ok = wavDecodeToMono16(f, dst, freeFrames, frames, rate);
    f.close();
    if (!ok || frames == 0) return -1;
    return registerSample(filename, frames, rate, FMT_PCM16, OWN_ONESHOT);
}

// ---------- deck material ----------
// Two passes over the file so nothing but the destination is held in RAM:
// pass 1 measures DC and peak, pass 2 filters, resamples, normalises, stores.
int samplerLoadDeck(const char* path, const char* name, uint8_t owner,
                    uint8_t qual, float startSec, bool* cut) {
    if (cut) *cut = false;
    if (!g_arena || freeSlot() < 0 || g_tailHeld) return -1;
    File f = SD.open(path, FILE_READ);
    if (!f) return -1;
    WavReader w;
    if (!w.open(f)) { f.close(); return -1; }

    uint32_t startFrame = (uint32_t)(startSec * (float)w.rate);
    if (startFrame >= w.frames) startFrame = 0;
    uint32_t srcFrames = w.frames - startFrame;
    double   ratio     = (double)w.rate / (double)SAMPLE_RATE;   // src frames per output frame
    uint32_t want      = (uint32_t)((double)srcFrames / ratio);
    uint32_t freeB     = samplerFreeBytes() & ~1u;

    uint8_t  fmt;
    uint32_t outFrames = want;
    if (qual == QUAL_16BIT)      fmt = FMT_PCM16;
    else if (qual == QUAL_LOFI)  fmt = FMT_ULAW;
    else                         fmt = (want * 2 <= freeB) ? FMT_PCM16 : FMT_ULAW;
    uint32_t cap = (fmt == FMT_PCM16) ? freeB / 2 : freeB;
    if (outFrames > cap) { outFrames = cap; if (cut) *cut = true; }
    if (outFrames < 64) { f.close(); return -1; }
    srcFrames = (uint32_t)((double)outFrames * ratio);
    if (srcFrames > w.frames - startFrame) srcFrames = w.frames - startFrame;

    static int16_t buf[256];

    // pass 1: DC + peak
    int64_t sum = 0; int32_t lo = 0, hi = 0;
    w.seekFrame(startFrame);
    for (uint32_t left = srcFrames; left; ) {
        uint32_t n = w.read(buf, left > 256 ? 256 : left);
        if (!n) break;
        for (uint32_t i = 0; i < n; i++) { sum += buf[i]; if (buf[i] < lo) lo = buf[i]; if (buf[i] > hi) hi = buf[i]; }
        left -= n;
    }
    float dc   = (float)((double)sum / (double)(srcFrames ? srcFrames : 1));
    float peak = fmaxf((float)hi - dc, dc - (float)lo);
    float gain = (peak > 1.0f) ? 29200.0f / peak : 1.0f;   // about -1 dBFS
    if (gain > 8.0f) gain = 8.0f;                           // don't amplify a noise floor

    // pass 2: [1 2 1] low-pass when decimating, then linear resample
    uint8_t* dst8  = g_arena + g_arenaUsed;
    int16_t* dst16 = (int16_t*)dst8;
    bool     lp    = ratio > 1.4;
    float    x1 = 0, x2 = 0, fprev = 0;
    double   nextOut = 0.0;
    uint32_t k = 0, out = 0;

    w.seekFrame(startFrame);
    for (uint32_t left = srcFrames; left && out < outFrames; ) {
        uint32_t n = w.read(buf, left > 256 ? 256 : left);
        if (!n) break;
        for (uint32_t i = 0; i < n && out < outFrames; i++) {
            float x  = ((float)buf[i] - dc) * gain;
            float fl = lp ? (x2 + 2.0f * x1 + x) * 0.25f : x;
            x2 = x1; x1 = x;
            while (nextOut <= (double)k && out < outFrames) {
                float frac = (float)(nextOut - ((double)k - 1.0));
                float v = fprev + (fl - fprev) * frac;
                int32_t s = (int32_t)v;
                if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
                if (fmt == FMT_PCM16) dst16[out] = (int16_t)s;
                else                  dst8[out]  = ulawEncode(s);
                out++;
                nextOut += ratio;
            }
            fprev = fl; k++;
        }
        left -= n;
    }
    f.close();
    if (out < 64) return -1;
    return registerSample(name, out, SAMPLE_RATE, fmt, owner);
}

SampleVoice g_previewVoice;   // zero-initialized: inactive
