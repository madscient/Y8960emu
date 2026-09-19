// keyoff_retrigger_test.cpp
// 回帰テスト: 同じチャンネルへの KEY OFF → KEY ON がキューに同居しても、
// KEY OFF が観測されることを確認する。次の3つの条件で見る。
//   batch : 1回の generate() にまとめて渡す
//   crowd : 先に他の5チャンネルが同じく KEY OFF → KEY ON し、
//           generate() を 240 サンプルずつ呼ぶ (衝突が多く1回の呼び出しに収まらない)
//   tiny  : generate() を 1 サンプルずつ呼ぶ
//
// 判定は2種類。
//   dip : 減衰しない持続音。KEY OFF が観測されればリリースで音量が一度大きく
//         落ちてから戻る。観測されない (または一瞬しか観測されない) と落ちない。
//   hit : 減衰しきった打楽器。KEY OFF が観測されれば再び打撃が立ち上がる。
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

// 1ms 窓ごとのピークの最小値。発音は約3.1kHz (1窓に約15周期) なので、
// 波形の位相では窓ピークが落ちない。
static float minWindowPeak(const std::vector<float>& v) {
    constexpr size_t kWin = kRate / 1000;
    float m = 1e9f;
    for (size_t i = 0; i + kWin <= v.size(); i += kWin / 2)
        m = std::fmin(m, peak(v, i, i + kWin));
    return m;
}

struct Write3 { uint8_t reg, on, off; };

// 対象チャンネルは ch0。モジュレータは TL=63 で無音、キャリアは AR=15 /
// DR=0 / SL=0 の持続音で RR=15 (KEY OFF で即座に消える)。
// ch1-5 は衝突を起こすためだけのチャンネルで、音は出さない
// (OPL2EX はリセット値の AR=0 でアタックしない。OPLLEX は F-Number=0)。
static void setupOpl2ex(FmEngine& eng, uint32_t id) {
    eng.write(id, 0x20, 0x21); eng.write(id, 0x40, 0x3F); eng.write(id, 0x60, 0xFF); eng.write(id, 0x80, 0x0F);
    eng.write(id, 0x23, 0x21); eng.write(id, 0x43, 0x00); eng.write(id, 0x63, 0xF0); eng.write(id, 0x83, 0x0F);
    eng.write(id, 0xA0, 0x00);
}
static const Write3 kOpl2exTarget = { 0xB0, 0x3E, 0x1E };
static const Write3 kOpl2exOthers[] = {
    { 0xB1, 0x20, 0x00 }, { 0xB2, 0x20, 0x00 }, { 0xB3, 0x20, 0x00 },
    { 0xB4, 0x20, 0x00 }, { 0xB5, 0x20, 0x00 },
};

static void setupOpllex(FmEngine& eng, uint32_t id) {
    eng.write(id, 0x00, 0x21); eng.write(id, 0x01, 0x21);
    eng.write(id, 0x02, 0x3F); eng.write(id, 0x03, 0x00);
    eng.write(id, 0x04, 0xFF); eng.write(id, 0x05, 0xF0);
    eng.write(id, 0x06, 0x0F); eng.write(id, 0x07, 0x0F);
    eng.write(id, 0x10, 0x00); eng.write(id, 0x30, 0x00);
}
static const Write3 kOpllexTarget = { 0x20, 0x1F, 0x0F };
static const Write3 kOpllexOthers[] = {
    { 0x21, 0x10, 0x00 }, { 0x22, 0x10, 0x00 }, { 0x23, 0x10, 0x00 },
    { 0x24, 0x10, 0x00 }, { 0x25, 0x10, 0x00 },
};

// リズムモードの BD。ch6-8 の F-Number/Block は MSX-MUSIC の慣用値。
static void setupOpllexRhythm(FmEngine& eng, uint32_t id) {
    eng.write(id, 0x16, 0x20); eng.write(id, 0x26, 0x05);
    eng.write(id, 0x17, 0x50); eng.write(id, 0x27, 0x05);
    eng.write(id, 0x18, 0xC0); eng.write(id, 0x28, 0x01);
    eng.write(id, 0x36, 0x00); eng.write(id, 0x37, 0x00); eng.write(id, 0x38, 0x00);
    eng.write(id, 0x0E, 0x20);
}
static const Write3 kOpllexRhythmTarget = { 0x0E, 0x30, 0x20 };

enum class Judge { Dip, Hit };

struct Case {
    const char* chip;
    void (*setup)(FmEngine&, uint32_t);
    Write3 target;
    const Write3* others;
    size_t otherCount;
    Judge judge;
};

enum class Mode { Batch, Crowd, Tiny };

static bool run(const Case& c, Mode mode) {
    FmEngine eng(kRate);
    const uint32_t id = eng.addChipByName(c.chip);
    c.setup(eng, id);
    for (size_t i = 0; i < c.otherCount; ++i) eng.write(id, c.others[i].reg, c.others[i].on);
    eng.write(id, c.target.reg, c.target.on);

    std::vector<float> l(kRate), r(kRate);
    eng.generate(l.data(), r.data(), kRate);
    const float attack = peak(l, 0, 2048);
    const float held   = peak(l, kRate - 2048, kRate);

    if (mode == Mode::Crowd)
        for (size_t i = 0; i < c.otherCount; ++i) {
            eng.write(id, c.others[i].reg, c.others[i].off);
            eng.write(id, c.others[i].reg, c.others[i].on);
        }
    eng.write(id, c.target.reg, c.target.off);
    eng.write(id, c.target.reg, c.target.on);

    const uint32_t chunk = (mode == Mode::Batch) ? 4096 : (mode == Mode::Crowd) ? 240 : 1;
    std::vector<float> l2(4800), r2(4800);
    for (uint32_t pos = 0; pos < l2.size(); pos += chunk) {
        const uint32_t n = std::min<uint32_t>(chunk, static_cast<uint32_t>(l2.size()) - pos);
        eng.generate(l2.data() + pos, r2.data() + pos, n);
    }

    const char* modeName = (mode == Mode::Batch) ? "batch" : (mode == Mode::Crowd) ? "crowd" : "tiny ";
    bool ok;
    if (c.judge == Judge::Dip) {
        const float dip  = minWindowPeak(l2);
        const float tail = peak(l2, l2.size() - 480, l2.size());
        ok = held > 0.05f && dip < held * 0.3f && tail > held * 0.8f;
        std::printf("[%s] %-6s %s dip : held=%.4f min1ms=%.4f tail=%.4f\n",
                    ok ? "OK" : "FAIL", c.chip, modeName, held, dip, tail);
    } else {
        const float retrig = peak(l2, 0, l2.size());
        ok = attack > 0.05f && held < attack * 0.1f && retrig > attack * 0.5f;
        std::printf("[%s] %-6s %s hit : attack=%.4f held=%.4f after OFF/ON=%.4f\n",
                    ok ? "OK" : "FAIL", c.chip, modeName, attack, held, retrig);
    }
    return ok;
}

int main() {
    const Case cases[] = {
        { "OPL2EX", setupOpl2ex,       kOpl2exTarget,       kOpl2exOthers, 5, Judge::Dip },
        { "OPLLEX", setupOpllex,       kOpllexTarget,       kOpllexOthers, 5, Judge::Dip },
        { "OPLLEX", setupOpllexRhythm, kOpllexRhythmTarget, kOpllexOthers, 5, Judge::Hit },
    };
    bool ok = true;
    for (const Case& c : cases)
        for (Mode m : { Mode::Batch, Mode::Crowd, Mode::Tiny })
            ok = run(c, m) && ok;
    return ok ? 0 : 1;
}
