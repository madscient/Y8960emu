// keyoff_retrigger_test.cpp
// 回帰テスト: 同じチャンネルへの KEY OFF → KEY ON が1回の FmEngine::generate()
// 呼び出しのキューに同居しても、KEY OFF が観測されて再アタックが起きることを
// 確認する。
//
// 手順: 減衰して十分小さくなるまでキーオンを保持した後、KEY OFF と KEY ON を
// 続けて書き込み、その後の出力が再アタックで大きく戻るかを見る。KEY OFF が
// 無視されると再アタックが起きず、減衰したままの小さい出力が続く。
//
#include "FmEngine.h"
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr uint32_t kRate = 48000;

static float peak(const std::vector<float>& v, size_t from, size_t to) {
    float p = 0.0f;
    for (size_t i = from; i < to; ++i) p = std::fmax(p, std::fabs(v[i]));
    return p;
}

// キーオン中の音色: モジュレータは TL=63 で無音、キャリアは AR=15 で即座に
// 立ち上がり、DR=8 / SL=15 で持続音のまま無音近くまで減衰、RR=15 で即座に
// 消える。
static void setupOpl2ex(FmEngine& eng, uint32_t id) {
    eng.write(id, 0x20, 0x21); eng.write(id, 0x40, 0x3F); eng.write(id, 0x60, 0xFF); eng.write(id, 0x80, 0x0F);
    eng.write(id, 0x23, 0x21); eng.write(id, 0x43, 0x00); eng.write(id, 0x63, 0xF8); eng.write(id, 0x83, 0xFF);
    eng.write(id, 0xA0, 0x44);
}

static void setupOpllex(FmEngine& eng, uint32_t id) {
    eng.write(id, 0x00, 0x21); eng.write(id, 0x01, 0x21);
    eng.write(id, 0x02, 0x3F); eng.write(id, 0x03, 0x00);
    eng.write(id, 0x04, 0xFF); eng.write(id, 0x05, 0xF8);
    eng.write(id, 0x06, 0x0F); eng.write(id, 0x07, 0xFF);
    eng.write(id, 0x10, 0x80); eng.write(id, 0x30, 0x00);
}

struct Case {
    const char* chip;
    void (*setup)(FmEngine&, uint32_t);
    uint8_t keyReg, keyOn, keyOff;
};

static bool run(const Case& c) {
    FmEngine eng(kRate);
    const uint32_t id = eng.addChipByName(c.chip);
    c.setup(eng, id);
    eng.write(id, c.keyReg, c.keyOn);

    std::vector<float> l(kRate), r(kRate);
    eng.generate(l.data(), r.data(), kRate);
    const float held = peak(l, kRate - 2048, kRate);
    const float attack = peak(l, 0, 2048);

    eng.write(id, c.keyReg, c.keyOff);
    eng.write(id, c.keyReg, c.keyOn);
    std::vector<float> l2(2048), r2(2048);
    eng.generate(l2.data(), r2.data(), 2048);
    const float retrig = peak(l2, 0, 2048);

    const bool ok = attack > 0.05f && held < attack * 0.1f && retrig > attack * 0.5f;
    std::printf("[%s] %s: attack=%.4f held=%.4f after OFF/ON=%.4f\n",
                ok ? "OK" : "FAIL", c.chip, attack, held, retrig);
    return ok;
}

int main() {
    const Case cases[] = {
        { "OPL2EX", setupOpl2ex, 0xB0, 0x32, 0x12 },
        { "OPLLEX", setupOpllex, 0x20, 0x18, 0x08 },
    };
    bool ok = true;
    for (const Case& c : cases) ok = run(c) && ok;
    return ok ? 0 : 1;
}
