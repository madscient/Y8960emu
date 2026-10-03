// smoke_test.cpp
// FmEngineApi 経由でY8960拡張2チップを一通り操作し、DLL がエクスポートする
// 関数をすべて呼んで、仕様どおりに応答することを確認する。
// (拡張SSG部はスコープ外。DSAemuEngineの"SSG"をアプリ側で使う)
//
// 全件通れば終了コード 0。
#include "FmEngineApi.h"
#include "test_clocks.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static constexpr uint32_t kRate = 48000;

static int  g_fail = 0;
static char msg[256];

static void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? "OK" : "FAIL", what);
    if (!ok) ++g_fail;
}

static bool streq(const char* a, const char* b) {
    return a && b && std::strcmp(a, b) == 0;
}

static float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    float d = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) d = std::fmax(d, std::fabs(a[i] - b[i]));
    return d;
}

static uint32_t addChip(FmEngineHandle eng, const char* name) {
    uint32_t id = UINT32_MAX;
    const FmResult r = FmEngine_AddChip(eng, name, kTestClock, &id);
    std::snprintf(msg, sizeof msg, "AddChip(%s) -> %d, id=%u", name, r, id);
    check(r == FM_OK && streq(FmEngine_GetChipName(eng, id), name), msg);

    // 適当にレジスタを叩いてみる
    for (uint32_t reg = 0; reg < 0x20; ++reg)
        FmEngine_Write(eng, id, static_cast<uint8_t>(reg), 0x00, 0);
    return id;
}

// 拡張OPL2部の ADPCM-B を外部メモリから鳴らし、L 側を返す。
// memory が nullptr なら FmEngine_SetMemory を呼ばない。
// lateFill なら、0 で埋めたメモリを渡してから中身を書く。
static std::vector<float> playAdpcm(const char* memory, const std::vector<uint8_t>& data,
                                    FmResult* result, bool lateFill = false) {
    std::vector<uint8_t> mem(data.size(), 0x00);
    if (!lateFill) std::copy(data.begin(), data.end(), mem.begin());

    FmEngineHandle eng = FmEngine_Create(kRate);
    uint32_t id = 0;
    FmEngine_AddChip(eng, "OPL2EX", kTestClock, &id);
    if (memory)
        *result = FmEngine_SetMemory(eng, id, memory, mem.data(),
                                     static_cast<uint32_t>(mem.size()));
    if (lateFill) std::copy(data.begin(), data.end(), mem.begin());

    static const uint8_t kWrites[][2] = {
        { 0x08, 0x00 },                  // RAM モード (番地は4バイト単位)
        { 0x09, 0x00 }, { 0x0A, 0x00 },  // 開始番地
        { 0x0B, 0xFF }, { 0x0C, 0x00 },  // 終了番地 (先頭から 1024 バイト)
        { 0x10, 0x00 }, { 0x11, 0x40 },  // DELTA-N
        { 0x12, 0xFF },                  // レベル
        { 0x07, 0xA0 },                  // START | MEMDATA
    };
    for (const auto& w : kWrites) FmEngine_Write(eng, id, w[0], w[1], 0);

    std::vector<float> l(4800), r(4800);
    FmEngine_Generate(eng, l.data(), r.data(), 4800);
    FmEngine_Destroy(eng);
    return l;
}

// チップにレジスタ経由でメモリへ書かせ、SetMemory に渡した data に届いた
// バイト数を返す。data は書き込めるとは限らないので、0 でなければならない。
static size_t bytesWrittenByChip() {
    std::vector<uint8_t> data(1024, 0x00);
    FmEngineHandle eng = FmEngine_Create(kRate);
    uint32_t id = 0;
    FmEngine_AddChip(eng, "OPL2EX", kTestClock, &id);
    FmEngine_SetMemory(eng, id, "ADPCM_B", data.data(), static_cast<uint32_t>(data.size()));

    static const uint8_t kWrites[][2] = {
        { 0x08, 0x00 },
        { 0x09, 0x00 }, { 0x0A, 0x00 },
        { 0x0B, 0xFF }, { 0x0C, 0x00 },
        { 0x07, 0x60 },                  // REC | MEMDATA (メモリへの書き込み)
        { 0x0F, 0xAA }, { 0x0F, 0xAA }, { 0x0F, 0xAA }, { 0x0F, 0xAA },
    };
    for (const auto& w : kWrites) FmEngine_Write(eng, id, w[0], w[1], 0);

    std::vector<float> l(64), r(64);
    FmEngine_Generate(eng, l.data(), r.data(), 64);
    FmEngine_Destroy(eng);

    size_t changed = 0;
    for (uint8_t b : data) if (b != 0x00) ++changed;
    return changed;
}

static void testMemory(FmEngineHandle eng, uint32_t opl2ex, uint32_t opllex) {
    check(FmEngine_GetMemoryCount(eng, opl2ex) == 1 &&
          streq(FmEngine_GetMemoryName(eng, opl2ex, 0), "ADPCM_B") &&
          FmEngine_GetMemoryName(eng, opl2ex, 1) == nullptr,
          "OPL2EX has one external memory, ADPCM_B");
    check(FmEngine_GetMemoryCount(eng, opllex) == 0 &&
          FmEngine_GetMemoryName(eng, opllex, 0) == nullptr,
          "OPLLEX has no external memory");
    check(FmEngine_GetMemoryCount(eng, 1000) == 0 &&
          FmEngine_GetMemoryName(eng, 1000, 0) == nullptr,
          "unknown chip_id: GetMemoryCount is 0, GetMemoryName is null");

    // ニブルが 7,7,F,F の繰り返し。メモリが無いとき (0 が読める) の出力と
    // はっきり違う、振れ幅の大きい波になる。
    std::vector<uint8_t> data(1024);
    for (size_t i = 0; i < data.size(); ++i) data[i] = (i & 1) ? 0xFF : 0x77;
    const uint32_t size = static_cast<uint32_t>(data.size());

    auto rejected = [&](uint32_t id, const char* memory) {
        return FmEngine_SetMemory(eng, id, memory, data.data(), size) == FM_ERR_INVALID_ARG;
    };
    check(rejected(opl2ex, "PCM") && rejected(opl2ex, "adpcm_b") && rejected(opl2ex, nullptr),
          "SetMemory rejects a name the chip does not have, the wrong case, and null");
    check(rejected(opllex, "ADPCM_B") && rejected(1000, "ADPCM_B"),
          "SetMemory rejects a chip without the memory and an unknown chip_id");

    FmResult set = FM_ERR_UNAVAILABLE, bad = FM_OK;
    const std::vector<float> none  = playAdpcm(nullptr,   data, nullptr);
    const std::vector<float> named = playAdpcm("ADPCM_B", data, &set);
    const std::vector<float> wrong = playAdpcm("PCM",     data, &bad);
    std::snprintf(msg, sizeof msg,
                  "SetMemory(ADPCM_B) -> %d and the chip plays it (max diff from no memory %.4f)",
                  set, maxAbsDiff(named, none));
    check(set == FM_OK && maxAbsDiff(named, none) > 0.05f, msg);
    std::snprintf(msg, sizeof msg,
                  "SetMemory(PCM) -> %d and the output stays as with no memory (max diff %.4f)",
                  bad, maxAbsDiff(wrong, none));
    check(bad == FM_ERR_INVALID_ARG && maxAbsDiff(wrong, none) == 0.0f, msg);

    // 2回路でメモリを共有するアプリケーションは、渡した後で中身を書く。
    FmResult late = FM_ERR_UNAVAILABLE;
    const std::vector<float> filledLater = playAdpcm("ADPCM_B", data, &late, true);
    std::snprintf(msg, sizeof msg,
                  "data is referenced, not copied: bytes written after SetMemory are played (max diff %.4f)",
                  maxAbsDiff(filledLater, named));
    check(late == FM_OK && maxAbsDiff(filledLater, named) == 0.0f, msg);

    const size_t written = bytesWrittenByChip();
    std::snprintf(msg, sizeof msg,
                  "chip writes never reach the data given to SetMemory (%u bytes changed)",
                  static_cast<unsigned>(written));
    check(written == 0, msg);
}

int main() {
    FmEngineHandle eng = FmEngine_Create(kRate);
    if (!eng) { std::printf("[FAIL] Create\n"); return 1; }

    std::printf("supported chips: %u\n", FmEngine_Inquiry(eng));
    for (uint32_t i = 0; i < FmEngine_Inquiry(eng); ++i)
        std::printf("  - %s\n", FmEngine_GetSupportedChip(eng, i));
    check(FmEngine_GetSupportedChip(eng, FmEngine_Inquiry(eng)) == nullptr,
          "GetSupportedChip is null past the last chip");
    check(FmEngine_GetSampleRate(eng) == kRate, "GetSampleRate");

    const uint32_t opl2ex = addChip(eng, "OPL2EX");
    const uint32_t opllex = addChip(eng, "OPLLEX");

    // エラーハンドリング確認。拒否したときはチップが増えない。
    uint32_t dummy = 12345;
    FmResult r = FmEngine_AddChip(eng, "NOSUCHCHIP", kTestClock, &dummy);
    std::snprintf(msg, sizeof msg, "AddChip(NOSUCHCHIP) -> %d (expect %d)", r, FM_ERR_UNKNOWN_CHIP);
    check(r == FM_ERR_UNKNOWN_CHIP, msg);
    r = FmEngine_AddChip(eng, "OPL2EX", 0, &dummy);
    std::snprintf(msg, sizeof msg, "AddChip(OPL2EX, clock=0) -> %d (expect %d)", r, FM_ERR_INVALID_ARG);
    check(r == FM_ERR_INVALID_ARG, msg);
    check(dummy == 12345 && FmEngine_GetChipName(eng, opllex + 1) == nullptr,
          "rejected AddChip adds no chip");

    float gl = 0.0f, gr = 0.0f;
    check(FmEngine_SetGain(eng, opl2ex, 0.25f, 0.75f) == FM_OK &&
          FmEngine_GetGain(eng, opl2ex, &gl, &gr) == FM_OK && gl == 0.25f && gr == 0.75f,
          "SetGain / GetGain round trip");

    testMemory(eng, opl2ex, opllex);

    // 波形生成
    std::vector<float> l(1024), r2(1024);
    FmResult gen = FmEngine_Generate(eng, l.data(), r2.data(), 1024);
    std::snprintf(msg, sizeof msg, "Generate -> %d", gen);
    check(gen == FM_OK, msg);

    bool any_nan = false, any_inf = false;
    for (float v : l) { if (std::isnan(v)) any_nan = true; if (std::isinf(v)) any_inf = true; }
    check(!any_nan, "NaN check");
    check(!any_inf, "Inf check");

    FmEngine_Destroy(eng);
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
