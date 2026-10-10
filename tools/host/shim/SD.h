#pragma once
#include "FS.h"

// The card is a directory on the host: $FILTRE_SD, default ./sd
class SDClass {
public:
    std::string root();
    File open(const char* path, const char* mode = FILE_READ);
    bool exists(const char* path);
    bool mkdir(const char* path);
    bool remove(const char* path);
};
extern SDClass SD;
