#pragma once
// FmChip.h
// Y8960拡張チップ (拡張SSG/拡張OPL2/拡張OPLL) を FmEngineApi 互換の
// FmChip インターフェースにラップする。
//
// 構成は madscient/YMEngine の FmChip.h パターンを踏襲している
// (LinearResampler / FmChip / MemoryYmfmInterface / FmChipImpl<T,Type>)。
// 標準ymfmチップ(OPNA/OPL3等)が必要な場合は YMEngine 側の FmChip.h と
// マージすること。本ファイルはY8960固有の3チップのみを対象とする。
//
// 依存: ymfm (https://github.com/aaronsgiles/ymfm)
//       dual_ssg.h / opl2ex.h / opllex.h (本プロジェクト src/)
//       C++17以上

#include "dual_ssg.h"
#include "opl2ex.h"
#include "opllex.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <cassert>
#include <algorithm>

// =========================================================
//  チップ種別列挙
// =========================================================
enum class ChipType {
    Y8960_SSG,    // 拡張SSG部  (YM2149相当 x2回路)
    Y8960_OPL2,   // 拡張OPL2部 (YM3812相当 + ADPCM-B、1回路分)
    Y8960_OPLLX,  // 拡張OPLL部 (YM2413相当 + プリセット音色バンク切替、1回路分)
};

// =========================================================
//  標準クロック定数
//  Y8960 CartridgeはMSX用カートリッジのため、MSX標準クロックを既定値とする。
// =========================================================
namespace FmClock {
    constexpr uint32_t Y8960_SSG   = 3'579'545;
    constexpr uint32_t Y8960_OPL2  = 3'579'545;
    constexpr uint32_t Y8960_OPLLX = 3'579'545;
}

// =========================================================
//  LinearResampler
//  チップのネイティブサンプルレートとエンジン出力レートが異なる場合に
//  線形補間で吸収する。
// =========================================================
class LinearResampler {
public:
    void setup(uint32_t src_rate, uint32_t dst_rate) {
        m_src_rate  = src_rate;
        m_dst_rate  = dst_rate;
        m_phase_inc = (static_cast<uint64_t>(src_rate) << 32) / dst_rate;
        m_phase     = 0;
        m_work_l.clear();
        m_work_r.clear();
    }

    bool isPassthrough() const { return m_src_rate == m_dst_rate; }

    template<typename GenFn>
    void process(GenFn&& generate_fn, float* out_l, float* out_r, uint32_t dst_samples) {
        if (isPassthrough()) {
            generate_fn(out_l, out_r, dst_samples);
            return;
        }

        const uint32_t phase_offset = static_cast<uint32_t>(m_phase >> 32);
        const uint32_t src_needed =
            phase_offset +
            static_cast<uint32_t>(
                (static_cast<uint64_t>(dst_samples) * m_src_rate) / m_dst_rate) + 2;

        m_work_l.resize(src_needed);
        m_work_r.resize(src_needed);
        generate_fn(m_work_l.data(), m_work_r.data(), src_needed);

        for (uint32_t di = 0; di < dst_samples; ++di) {
            const uint32_t int_part = static_cast<uint32_t>(m_phase >> 32);
            const float    frac     = static_cast<float>(m_phase & 0xFFFFFFFFull)
                                      * (1.0f / 4294967296.0f);

            const uint32_t i0 = (int_part     < src_needed) ? int_part     : src_needed - 1;
            const uint32_t i1 = (int_part + 1 < src_needed) ? int_part + 1 : src_needed - 1;

            out_l[di] = m_work_l[i0] + (m_work_l[i1] - m_work_l[i0]) * frac;
            out_r[di] = m_work_r[i0] + (m_work_r[i1] - m_work_r[i0]) * frac;

            m_phase += m_phase_inc;
        }

        const uint32_t consumed = static_cast<uint32_t>(m_phase >> 32);
        m_phase -= static_cast<uint64_t>(consumed) << 32;
    }

private:
    uint32_t m_src_rate = 0, m_dst_rate = 0;
    uint64_t m_phase_inc = 0, m_phase = 0;
    std::vector<float> m_work_l, m_work_r;
};

// =========================================================
//  FmChip インターフェース
// =========================================================
class FmChip {
public:
    virtual ~FmChip() = default;
    virtual void        write(uint32_t port, uint8_t reg, uint8_t value) = 0;
    virtual void        generate(float* out_l, float* out_r, uint32_t dst_samples) = 0;
    virtual void        setTargetRate(uint32_t target_rate) = 0;
    virtual uint32_t    nativeRate() const = 0;
    virtual ChipType    type()  const = 0;
    virtual const char* name()  const = 0;
    virtual uint32_t    clock() const = 0;

    // 外部メモリの設定 (拡張OPL2部のADPCM-B RAM/ROM用)
    virtual void        setMemory(ymfm::access_class access_type,
                                  const uint8_t* data, uint32_t size) {}
    virtual uint32_t    memorySize(ymfm::access_class access_type) const { return 0; }
};

// =========================================================
//  MemoryYmfmInterface
//  外部メモリアクセスに対応した ymfm_interface。
//  ADPCM_B領域は拡張OPL2部で使用する。
// =========================================================
class MemoryYmfmInterface : public ymfm::ymfm_interface {
public:
    void    ymfm_set_timer(uint32_t, int32_t) override {}
    void    ymfm_sync_mode_write(uint8_t)     override {}
    void    ymfm_sync_check_interrupts()      override {}
    void    ymfm_update_irq(bool)             override {}

    uint8_t ymfm_external_read(ymfm::access_class type, uint32_t address) override {
        const auto& mem = getRegion(type);
        if (mem.data && address < mem.size)
            return mem.data[address];
        return 0;
    }

    void ymfm_external_write(ymfm::access_class type,
                             uint32_t address, uint8_t data) override {
        auto& mem = getRegion(type);
        if (mem.writeable && mem.owned && address < mem.size)
            const_cast<uint8_t*>(mem.data)[address] = data;
    }

    void setMemory(ymfm::access_class type,
                   const uint8_t* data, uint32_t size) {
        auto& mem = getRegion(type);
        mem.data     = data;
        mem.size     = size;
        mem.owned    = false;
        mem.writeable = false;
    }

    void allocMemory(ymfm::access_class type, uint32_t size) {
        auto& mem = getRegion(type);
        mem.buf.assign(size, 0);
        mem.data      = mem.buf.data();
        mem.size      = size;
        mem.owned     = true;
        mem.writeable = true;
    }

    uint32_t memorySize(ymfm::access_class type) const {
        return getRegion(type).size;
    }

private:
    struct MemRegion {
        const uint8_t*      data      = nullptr;
        uint32_t            size      = 0;
        bool                owned     = false;
        bool                writeable = false;
        std::vector<uint8_t> buf;
    };

    // Y8960拡張OPL2部はADPCM-Bのみ使用する。
    MemRegion m_adpcm_b;

    MemRegion& getRegion(ymfm::access_class type) {
        switch (type) {
            case ymfm::ACCESS_ADPCM_B: return m_adpcm_b;
            default:                   return m_adpcm_b; // fallback
        }
    }
    const MemRegion& getRegion(ymfm::access_class type) const {
        return const_cast<MemoryYmfmInterface*>(this)->getRegion(type);
    }
};

// =========================================================
//  FmChipImpl<ChipImpl, TType>
// =========================================================
template<typename ChipImpl, ChipType TType>
class FmChipImpl final : public FmChip {
public:
    explicit FmChipImpl(uint32_t clock);

    void write(uint32_t port, uint8_t reg, uint8_t value) override {
        const uint32_t addr_offset = (port != 0) ? 2 : 0;
        const uint32_t data_offset = addr_offset + 1;
        m_chip.write(addr_offset, reg);
        m_chip.write(data_offset, value);
    }

    void generate(float* out_l, float* out_r, uint32_t dst_samples) override {
        m_resampler.process(
            [this](float* l, float* r, uint32_t n){ generateNative(l, r, n); },
            out_l, out_r, dst_samples);
    }

    void setTargetRate(uint32_t target_rate) override {
        m_target_rate = target_rate;
        m_resampler.setup(m_native_rate, target_rate);
    }

    void setMemory(ymfm::access_class access_type,
                   const uint8_t* data, uint32_t size) override {
        m_iface.setMemory(access_type, data, size);
    }

    uint32_t memorySize(ymfm::access_class access_type) const override {
        return m_iface.memorySize(access_type);
    }

    uint32_t    nativeRate() const override { return m_native_rate; }
    ChipType    type()       const override { return TType; }
    uint32_t    clock()      const override { return m_clock; }
    const char* name()       const override;

private:
    void generateNative(float* out_l, float* out_r, uint32_t n) {
        typename ChipImpl::output_data out_data{};
        constexpr float kScale = 1.0f / 32768.0f;
        constexpr uint32_t kOutputs =
            sizeof(out_data.data) / sizeof(out_data.data[0]);

        // 出力モードをTTypeで分類:
        //
        // Y8960実機はチップ外側に独立したデジタルミキサーを持ち、機能ブロック
        // (拡張SSG部・拡張OPL2部・拡張OPLL部それぞれ)ごとにパンポットを
        // 指定する設計になっている。そのため、チップ内部での左右パン付けは
        // 行わず、各FmChipインスタンスはモノラル(L=R)を返すだけにとどめ、
        // 実際のパン/ゲインは呼び出し側が FmEngine_SetGain(chip_id, gain_l, gain_r)
        // で指定する前提とする。これはハードウェア設計と対応が取れた最終仕様であり、
        // 「実機ミキサー仕様待ちの暫定」ではない。
        //
        //   Y8960_SSG   : OUTPUTS=6 (SSG1 3ch + SSG2 3ch)。SSG1/SSG2は
        //                 単一のI/Oポート対(7FEAh/7FEBh)を共有する1機能ブロック
        //                 のため、6ch全てを等分加算してモノラル化する。
        //
        //   Y8960_OPL2  : OUTPUTS=2 (melody, rhythm)。ADPCM-Bは
        //                 y8960opl2ex::generate() 内で既に加算済み。
        //                 data[0]+data[1] をモノラル化 (Y8950と同型のMixMono)。
        //
        //   Y8960_OPLLX : OUTPUTS=2 (melody, rhythm)。OPLLと同型のMixMono。
        constexpr bool isSsg =
            (TType == ChipType::Y8960_SSG);
        constexpr bool isMixMono =
            kOutputs >= 2 &&
            (TType == ChipType::Y8960_OPL2 || TType == ChipType::Y8960_OPLLX);

        for (uint32_t i = 0; i < n; ++i) {
            m_chip.generate(&out_data);
            if constexpr (isSsg) {
                int32_t mix = 0;
                for (uint32_t c = 0; c < kOutputs; ++c)
                    mix += out_data.data[c];
                out_l[i] = out_r[i] = static_cast<float>(mix) * kScale;
            } else if constexpr (isMixMono) {
                out_l[i] = out_r[i] = static_cast<float>(
                    out_data.data[0] + out_data.data[1]) * kScale;
            } else {
                out_l[i] = out_r[i] =
                    static_cast<float>(out_data.data[0]) * kScale;
            }
        }
    }

    MemoryYmfmInterface m_iface;
    ChipImpl           m_chip;
    uint32_t           m_clock;
    uint32_t           m_native_rate = 0;
    uint32_t           m_target_rate = 0;
    LinearResampler    m_resampler;
};

// =========================================================
//  name() 特殊化
// =========================================================
template<> inline const char* FmChipImpl<ymfm::y8960ssg,    ChipType::Y8960_SSG  >::name() const { return "Y8960 Extended SSG";   }
template<> inline const char* FmChipImpl<ymfm::y8960opl2ex, ChipType::Y8960_OPL2 >::name() const { return "Y8960 Extended OPL2";  }
template<> inline const char* FmChipImpl<ymfm::y8960opllex, ChipType::Y8960_OPLLX>::name() const { return "Y8960 Extended OPLL"; }

// =========================================================
//  コンストラクタ特殊化
//  Y8960拡張チップ3種はいずれも (ymfm_interface&) のみを取る
// =========================================================
template<>
inline FmChipImpl<ymfm::y8960ssg, ChipType::Y8960_SSG>::FmChipImpl(uint32_t clock)
    : m_chip(m_iface), m_clock(clock ? clock : FmClock::Y8960_SSG)
{ m_chip.reset(); m_native_rate = m_chip.sample_rate(m_clock); }

template<>
inline FmChipImpl<ymfm::y8960opl2ex, ChipType::Y8960_OPL2>::FmChipImpl(uint32_t clock)
    : m_chip(m_iface), m_clock(clock ? clock : FmClock::Y8960_OPL2)
{ m_chip.reset(); m_native_rate = m_chip.sample_rate(m_clock); }

template<>
inline FmChipImpl<ymfm::y8960opllex, ChipType::Y8960_OPLLX>::FmChipImpl(uint32_t clock)
    : m_chip(m_iface), m_clock(clock ? clock : FmClock::Y8960_OPLLX)
{ m_chip.reset(); m_native_rate = m_chip.sample_rate(m_clock); }

// =========================================================
//  ファクトリ関数 (ChipType版)
// =========================================================
inline std::unique_ptr<FmChip> createChip(ChipType type, uint32_t clock = 0) {
    auto resolve = [](uint32_t c, uint32_t def) { return c ? c : def; };
    switch (type) {
        case ChipType::Y8960_SSG:
            return std::make_unique<FmChipImpl<ymfm::y8960ssg, ChipType::Y8960_SSG>>(resolve(clock, FmClock::Y8960_SSG));
        case ChipType::Y8960_OPL2:
            return std::make_unique<FmChipImpl<ymfm::y8960opl2ex, ChipType::Y8960_OPL2>>(resolve(clock, FmClock::Y8960_OPL2));
        case ChipType::Y8960_OPLLX:
            return std::make_unique<FmChipImpl<ymfm::y8960opllex, ChipType::Y8960_OPLLX>>(resolve(clock, FmClock::Y8960_OPLLX));
    }
    return nullptr;
}

// =========================================================
//  文字列ベースファクトリ / チップ名列挙
// =========================================================
struct ChipEntry {
    const char* name;
    ChipType    type;
    uint32_t    defaultClock;
};

inline const ChipEntry* chipTable() {
    static const ChipEntry kTable[] = {
        { "Y8960_SSG",   ChipType::Y8960_SSG,   FmClock::Y8960_SSG   },
        { "Y8960_OPL2",  ChipType::Y8960_OPL2,  FmClock::Y8960_OPL2  },
        { "Y8960_OPLLX", ChipType::Y8960_OPLLX, FmClock::Y8960_OPLLX },
        { nullptr,       ChipType::Y8960_SSG,   0                    },  // sentinel
    };
    return kTable;
}

inline uint32_t chipTableSize() {
    uint32_t n = 0;
    for (const ChipEntry* e = chipTable(); e->name; ++e) ++n;
    return n;
}

inline std::unique_ptr<FmChip> createChipByName(const char* name, uint32_t clock = 0) {
    if (!name) return nullptr;
    for (const ChipEntry* e = chipTable(); e->name; ++e) {
        if (std::strcmp(e->name, name) == 0)
            return createChip(e->type, clock ? clock : e->defaultClock);
    }
    return nullptr;
}

inline const char* chipNameByIndex(uint32_t index) {
    const ChipEntry* e = chipTable();
    uint32_t i = 0;
    for (; e->name; ++e, ++i)
        if (i == index) return e->name;
    return nullptr;
}
