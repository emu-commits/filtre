#include "SD.h"
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

uint32_t micros() { static uint32_t t = 0; return t += 137; }   // deterministic
uint32_t millis() { return micros() / 1000; }

SDClass SD;
bool g_needRedraw = false;
volatile uint32_t g_rsmpArmFrames = 0;

std::string SDClass::root() { const char* e = getenv("FILTRE_SD"); return e ? e : "sd"; }

File SDClass::open(const char* path, const char* mode) {
    File f; f.path = root() + path;
    struct stat st;
    if (mode[0] == 'r' && stat(f.path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { f.dp = opendir(f.path.c_str()); return f; }
    f.fp = fopen(f.path.c_str(), mode[0] == 'r' ? "rb" : "wb+");
    return f;
}
bool SDClass::exists(const char* path) { struct stat st; return stat((root() + path).c_str(), &st) == 0; }
bool SDClass::mkdir(const char* path)  { return ::mkdir((root() + path).c_str(), 0755) == 0; }
bool SDClass::remove(const char* path) { return ::unlink((root() + path).c_str()) == 0; }

File File::openNextFile() {
    File f;
    if (!dp) return f;
    while (struct dirent* e = readdir(dp)) {
        if (e->d_name[0] == '.') continue;
        f.path = path + "/" + e->d_name;
        struct stat st;
        if (stat(f.path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) f.dp = opendir(f.path.c_str());
        else f.fp = fopen(f.path.c_str(), "rb");
        return f;
    }
    return f;
}
