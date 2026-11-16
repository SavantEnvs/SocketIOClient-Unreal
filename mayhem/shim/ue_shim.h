// Minimal stand-in for the two Unreal Engine string helpers sio_packet.cpp uses
// (FString(const char*), FCString::Atoi/Atoi64). Semantics follow UE's Linux implementation:
// base-10 strtol/strtoll, with the 32-bit result truncated.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>

typedef char TCHAR;

class FString
{
public:
    FString(const char* Str) : Data(Str) {}
    const TCHAR* operator*() const { return Data.c_str(); }
private:
    std::string Data;
};

struct FCString
{
    static int32_t Atoi(const TCHAR* Str) { return (int32_t)strtol(Str, nullptr, 10); }
    static int64_t Atoi64(const TCHAR* Str) { return (int64_t)strtoll(Str, nullptr, 10); }
};
