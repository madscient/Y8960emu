// smoke_test.cpp
// FmEngineApi 経由でY8960拡張3チップを一通り操作し、クラッシュしないか確認する。
#include "FmEngineApi.h"
#include <cstdio>
#include <vector>
#include <cmath>
#include <cstring>

static void checkChip(FmEngineHandle eng, const char* name) {
    uint32_t id = 0;
    FmResult r = FmEngine_AddChip(eng, name, 0, &id);
    if (r != FM_OK) {
        std::printf("[FAIL] AddChip(%s) -> %d\n", name, r);
        return;
    }
    std::printf("[OK] AddChip(%s) -> id=%u nativeRate=%u\n",
                name, id, FmEngine_GetNativeRate(eng, id));

    // 適当にレジスタを叩いてみる (無音でなければOK程度の確認)
    for (uint32_t reg = 0; reg < 0x20; ++reg)
        FmEngine_Write(eng, id, static_cast<uint8_t>(reg), 0x00, 0);
}

int main() {
    FmEngineHandle eng = FmEngine_Create(48000);
    if (!eng) { std::printf("[FAIL] Create\n"); return 1; }

    std::printf("supported chips: %u\n", FmEngine_Inquiry(eng));
    for (uint32_t i = 0; i < FmEngine_Inquiry(eng); ++i)
        std::printf("  - %s\n", FmEngine_GetSupportedChip(eng, i));

    checkChip(eng, "Y8960_SSG");
    checkChip(eng, "Y8960_OPL2");
    checkChip(eng, "Y8960_OPLLX");

    // 未知チップ名のエラーハンドリング確認
    uint32_t dummy = 0;
    FmResult r = FmEngine_AddChip(eng, "NOSUCHCHIP", 0, &dummy);
    std::printf("[%s] AddChip(NOSUCHCHIP) -> %d (expect %d)\n",
                (r == FM_ERR_UNKNOWN_CHIP) ? "OK" : "FAIL", r, FM_ERR_UNKNOWN_CHIP);

    // 波形生成
    std::vector<float> l(1024), r2(1024);
    FmResult gr = FmEngine_Generate(eng, l.data(), r2.data(), 1024);
    std::printf("[%s] Generate -> %d\n", (gr == FM_OK) ? "OK" : "FAIL", gr);

    bool any_nan = false, any_inf = false;
    for (float v : l) { if (std::isnan(v)) any_nan = true; if (std::isinf(v)) any_inf = true; }
    std::printf("[%s] NaN check\n", any_nan ? "FAIL" : "OK");
    std::printf("[%s] Inf check\n", any_inf ? "FAIL" : "OK");

    FmEngine_Destroy(eng);
    std::printf("done.\n");
    return 0;
}
