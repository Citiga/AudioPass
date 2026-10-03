#pragma once

#include <cstdint>
#include <cstddef>

// Clean-room layout of the VST 2.4 AEffect ABI. Field order and alignment match
// the binary interface hosts and plugins already share. This is not the
// Steinberg SDK.

namespace bv::vst2 {

constexpr int32_t kMagic = 0x56737450; // 'VstP'

constexpr int32_t kHasEditor = 1 << 0;
constexpr int32_t kCanReplacing = 1 << 4;
constexpr int32_t kProgramChunks = 1 << 5;
constexpr int32_t kIsSynth = 1 << 8;
constexpr int32_t kCanDoubleReplacing = 1 << 12;

constexpr int32_t kTransportPlaying = 1 << 1;
constexpr int32_t kNanosValid = 1 << 8;
constexpr int32_t kPpqPosValid = 1 << 9;
constexpr int32_t kTempoValid = 1 << 10;
constexpr int32_t kTimeSigValid = 1 << 13;

enum Opcode : int32_t {
    effOpen = 0,
    effClose = 1,
    effSetProgram = 2,
    effGetProgram = 3,
    effGetParamLabel = 6,
    effGetParamDisplay = 7,
    effGetParamName = 8,
    effSetSampleRate = 10,
    effSetBlockSize = 11,
    effMainsChanged = 12,
    effEditGetRect = 13,
    effEditOpen = 14,
    effEditClose = 15,
    effEditIdle = 19,
    effGetChunk = 23,
    effSetChunk = 24,
    effGetPlugCategory = 35,
    effGetEffectName = 45,
    effGetVendorString = 47,
    effGetProductString = 48,
    effGetVendorVersion = 49,
    effCanDo = 51,
    effGetVstVersion = 58,
    effStartProcess = 71,
    effStopProcess = 72,
    effSetProcessPrecision = 77,
};

enum MasterOpcode : int32_t {
    masterAutomate = 0,
    masterVersion = 1,
    masterCurrentId = 2,
    masterIdle = 3,
    masterGetTime = 7,
    masterIOChanged = 13,
    masterSizeWindow = 15,
    masterGetSampleRate = 16,
    masterGetBlockSize = 17,
    masterGetInputLatency = 18,
    masterGetOutputLatency = 19,
    masterGetCurrentProcessLevel = 23,
    masterGetAutomationState = 24,
    masterGetVendorString = 32,
    masterGetProductString = 33,
    masterGetVendorVersion = 34,
    masterCanDo = 37,
    masterGetLanguage = 38,
    masterUpdateDisplay = 42,
    masterBeginEdit = 43,
    masterEndEdit = 44,
};

struct TimeInfo {
    double samplePos = 0;
    double sampleRate = 0;
    double nanoSeconds = 0;
    double ppqPos = 0;
    double tempo = 120;
    double barStartPos = 0;
    double cycleStartPos = 0;
    double cycleEndPos = 0;
    int32_t timeSigNumerator = 4;
    int32_t timeSigDenominator = 4;
    int32_t smpteOffset = 0;
    int32_t smpteFrameRate = 0;
    int32_t samplesToNextClock = 0;
    int32_t flags = 0;
};

struct Rect {
    int16_t top = 0;
    int16_t left = 0;
    int16_t bottom = 0;
    int16_t right = 0;
};

struct Effect;

using DispatchFn = intptr_t(__cdecl*)(Effect*, int32_t, int32_t, intptr_t, void*, float);
using ProcessFn = void(__cdecl*)(Effect*, float**, float**, int32_t);
using SetParameterFn = void(__cdecl*)(Effect*, int32_t, float);
using GetParameterFn = float(__cdecl*)(Effect*, int32_t);
using ProcessDoubleFn = void(__cdecl*)(Effect*, double**, double**, int32_t);
using MasterFn = intptr_t(__cdecl*)(Effect*, int32_t, int32_t, intptr_t, void*, float);
using MainFn = Effect*(__cdecl*)(MasterFn);

struct Effect {
    int32_t magic = 0;
    DispatchFn dispatcher = nullptr;
    ProcessFn process = nullptr;
    SetParameterFn setParameter = nullptr;
    GetParameterFn getParameter = nullptr;
    int32_t numPrograms = 0;
    int32_t numParams = 0;
    int32_t numInputs = 0;
    int32_t numOutputs = 0;
    int32_t flags = 0;
    intptr_t res1 = 0;
    intptr_t res2 = 0;
    int32_t initialDelay = 0;
    int32_t realQualities = 0;
    int32_t offQualities = 0;
    float ioRatio = 0;
    void* object = nullptr;
    void* user = nullptr;
    int32_t uniqueId = 0;
    int32_t version = 0;
    ProcessFn processReplacing = nullptr;
    ProcessDoubleFn processDoubleReplacing = nullptr;
    char future[56]{};
};

static_assert(sizeof(void*) == 8, "VST2 hosting is 64-bit only");
static_assert(offsetof(Effect, dispatcher) == 8);
static_assert(offsetof(Effect, numPrograms) == 40);
static_assert(offsetof(Effect, flags) == 56);
static_assert(offsetof(Effect, res1) == 64);
static_assert(offsetof(Effect, object) == 96);
static_assert(offsetof(Effect, uniqueId) == 112);
static_assert(offsetof(Effect, processReplacing) == 120);
static_assert(sizeof(Effect) == 192);

} // namespace bv::vst2
