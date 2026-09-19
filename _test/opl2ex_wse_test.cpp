// opl2ex_wse_test.cpp
// 回帰テスト: 拡張OPL2部の波形選択 (reg 0xE0-0xF5) は、YM3812 と同じく
// WSE (reg 0x01 bit5) を立てたときだけ効くことを確認する。
//
// キャリアに波形1 (半波サイン) を指定して鳴らし、出力の負側を見る。
//   WSE=0 : 波形選択は無効でサインのまま → 負側が出る
//   WSE=1 : 半波サイン → 負側が出ない
//
#include "FmEngine.h"
#include <algorithm>
#include <cstdio>
#include <vector>

static float minAfterAttack(bool wse) {
    FmEngine eng(48000);
    const uint32_t id = eng.addChipByName("OPL2EX");
    if (wse) eng.write(id, 0x01, 0x20);
    eng.write(id, 0x20, 0x21); eng.write(id, 0x40, 0x3F); eng.write(id, 0x60, 0xFF); eng.write(id, 0x80, 0x0F);
    eng.write(id, 0x23, 0x21); eng.write(id, 0x43, 0x00); eng.write(id, 0x63, 0xF0); eng.write(id, 0x83, 0x0F);
    eng.write(id, 0xE3, 0x01);
    eng.write(id, 0xA0, 0x44); eng.write(id, 0xB0, 0x32);
    std::vector<float> l(4800), r(4800);
    eng.generate(l.data(), r.data(), 4800);
    return *std::min_element(l.begin() + 1000, l.end());
}

int main() {
    const float off = minAfterAttack(false);
    const float on  = minAfterAttack(true);
    const bool okOff = off < -0.05f;
    const bool okOn  = on  > -0.01f;
    std::printf("[%s] WSE=0: waveform select ignored (min=%.4f, expect < -0.05)\n", okOff ? "OK" : "FAIL", off);
    std::printf("[%s] WSE=1: half-sine selected    (min=%.4f, expect > -0.01)\n", okOn ? "OK" : "FAIL", on);
    return (okOff && okOn) ? 0 : 1;
}
