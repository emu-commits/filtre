// Host shim: just enough Arduino to build the FILTRE engine natively.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>

uint32_t micros();
uint32_t millis();
static inline void delay(uint32_t) {}
#define constrain(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

class String {
public:
    std::string s;
    String() {}
    String(const char* c) : s(c ? c : "") {}
    String(const std::string& c) : s(c) {}
    int lastIndexOf(char c) const { size_t p = s.rfind(c); return p == std::string::npos ? -1 : (int)p; }
    String substring(int a) const { return String(s.substr(a)); }
    String substring(int a, int b) const { return String(s.substr(a, b - a)); }
    bool endsWith(const char* suf) const {
        size_t n = strlen(suf);
        return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
    }
    bool startsWith(const char* pre) const { return s.compare(0, strlen(pre), pre) == 0; }
    const char* c_str() const { return s.c_str(); }
};
