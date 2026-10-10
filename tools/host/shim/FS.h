#pragma once
#include "Arduino.h"
#include <dirent.h>

#define FILE_READ  "r"
#define FILE_WRITE "w"

class File {
public:
    FILE* fp = nullptr;
    DIR*  dp = nullptr;
    std::string path;

    operator bool() const { return fp || dp; }
    size_t read(uint8_t* b, size_t n) { return fp ? fread(b, 1, n, fp) : 0; }
    size_t write(const uint8_t* b, size_t n) { return fp ? fwrite(b, 1, n, fp) : 0; }
    bool seek(uint32_t p) { return fp && fseek(fp, (long)p, SEEK_SET) == 0; }
    uint32_t position() { return fp ? (uint32_t)ftell(fp) : 0; }
    uint32_t size() { if (!fp) return 0; long c = ftell(fp); fseek(fp, 0, SEEK_END); long e = ftell(fp); fseek(fp, c, SEEK_SET); return (uint32_t)e; }
    int available() { return fp ? (int)(size() - position()) : 0; }
    void close() { if (fp) fclose(fp); if (dp) closedir(dp); fp = nullptr; dp = nullptr; }
    bool isDirectory() const { return dp != nullptr; }
    const char* name() const { size_t p = path.rfind('/'); return path.c_str() + (p == std::string::npos ? 0 : p + 1); }
    File openNextFile();
};
