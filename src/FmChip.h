#pragma once
// FmChip.h
// Y8960拡張チップ (拡張OPL2/拡張OPLL) を FmEngineApi 互換の
// FmChip インターフェースにラップする。
//
// 構成は madscient/YMEngine の FmChip.h パターンを踏襲している
// (LinearResampler / FmChip / MemoryYmfmInterface / FmChipImpl<T,Type>)。
// 標準ymfmチップ(OPNA/OPL3等)が必要な場合は YMEngine 側の FmChip.h と
// マージすること。本ファイルはY8960固有の2チップのみを対象とする
// （拡張SSG部はスコープ外。理由は README/doc/CHANGELOG.md 参照）。
//
// 依存: ymfm (https://github.com/aaronsgiles/ymfm)
//       opl2ex.h / opllex.h (本プロジェクト src/)
//       C++17以上

#include "opl2ex.h"
#include "opllex.h"

#include <cstdint>
#include <cstring>
#include <array>
#include <memory>
#include <vector>
#include <cassert>
#include <algorithm>

// =========================================================
//  チップ種別列挙
// =========================================================
enum class ChipType {
    OPL2EX,   // 拡張OPL2部 (YM3812相当 + ADPCM-B、1回路分)
    OPLLEX,   // 拡張OPLL部 (YM2413相当 + プリセット音色バンク切替、1回路分)
};

// =========================================================
//  標準クロック定数
//  Y8960 CartridgeはMSX用カートリッジのため、MSX標準クロックを既定値とする。
// =========================================================
namespace FmClock {
    constexpr uint32_t OPL2EX = 3'579'545;
    constexpr uint32_t OPLLEX = 3'579'545;
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

    // このレジスタ書き込みでキーオン/オフ状態が変化するチャンネルスロットの
    // ビットマスク。変化しなければ 0。FmEngine::generate() が同一チャンネルの
    // 未観測な変化の重なりを検出するのに使う。
    // 直前値をキャッシュするため、write() と同じ順序で1回ずつ呼ぶこと。
    virtual uint64_t    keyOnTransitionMask(uint32_t port, uint8_t reg, uint8_t value) { return 0; }
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

    uint64_t keyOnTransitionMask(uint32_t port, uint8_t reg, uint8_t value) override {
        const uint8_t mask = keyBitMask(reg);
        if (mask == 0) return 0;

        const uint8_t prevMasked = m_lastKeyRegValue[reg] & mask;
        const uint8_t newMasked  = value & mask;
        m_lastKeyRegValue[reg] = value;
        if (prevMasked == newMasked) return 0;

        return keyChannelSlotMask(reg, static_cast<uint8_t>(prevMasked ^ newMasked));
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
        // (拡張OPL2部・拡張OPLL部それぞれ)ごとにパンポットを指定する設計になっている。
        // そのため、チップ内部での左右パン付けは行わず、各FmChipインスタンスは
        // モノラル(L=R)を返すだけにとどめ、実際のパン/ゲインは呼び出し側が
        // FmEngine_SetGain(chip_id, gain_l, gain_r) で指定する前提とする。
        //
        //   OPL2EX : OUTPUTS=2 (melody, rhythm)。ADPCM-Bは
        //                 y8960opl2ex::generate() 内で既に加算済み。
        //                 data[0]+data[1] をモノラル化 (Y8950と同型のMixMono)。
        //
        //   OPLLEX : OUTPUTS=2 (melody, rhythm)。OPLLと同型のMixMono。
        constexpr bool isMixMono =
            kOutputs >= 2 &&
            (TType == ChipType::OPL2EX || TType == ChipType::OPLLEX);

        for (uint32_t i = 0; i < n; ++i) {
            m_chip.generate(&out_data);
            if constexpr (isMixMono) {
                out_l[i] = out_r[i] = static_cast<float>(
                    out_data.data[0] + out_data.data[1]) * kScale;
            } else {
                out_l[i] = out_r[i] =
                    static_cast<float>(out_data.data[0]) * kScale;
            }
        }
    }

    // キーオン/オフに関係するビット。
    //   OPL2EX : reg 0xB0-0xB8 の bit5 / reg 0xBD の bit0-5 (リズム gate+楽器選択)
    //   OPLLEX : reg 0x20-0x28 の bit4 / reg 0x0E の bit0-5 (リズム gate+楽器選択)
    // キーオンビットと F-Number/Block が同一レジスタに同居するため、アドレス
    // だけで判定するとビブラート等の周波数書き換えにまで反応して余計な
    // 分割生成を招く。ビット単位で絞る。
    // ADPCM-B の START (reg 0x07) は ymfm 側で書き込み時に即時処理されるため
    // 対象外。
    // 両チップとも port によらず同一のレジスタ空間に書き込まれる (write() の
    // offset は下位1bitしか見られない) ため、port は判定に使わない。
    static uint8_t keyBitMask(uint8_t reg) {
        if constexpr (TType == ChipType::OPL2EX) {
            if (reg == 0xbd) return 0x3F;
            if (reg >= 0xb0 && reg <= 0xb8) return 0x20;
            return 0;
        } else if constexpr (TType == ChipType::OPLLEX) {
            if (reg == 0x0e) return 0x3F;
            if (reg >= 0x20 && reg <= 0x28) return 0x10;
            return 0;
        } else {
            return 0;
        }
    }

    // リズムレジスタの bit0-4 は独立した5打楽器、bit5 は全打楽器のマスター
    // ゲート。打楽器ごとに別スロットを割り当て、同時に鳴らすドラムどうしを
    // 衝突扱いしない。ゲートが変化したときは5スロットすべてを対象にする。
    static uint64_t rhythmSlotMask(uint8_t changedBits, uint32_t baseSlot) {
        uint64_t result = 0;
        for (uint32_t b = 0; b < 5; ++b)
            if (changedBits & (1u << b)) result |= (uint64_t{1} << (baseSlot + b));
        if (changedBits & 0x20u) result |= (uint64_t{0x1F} << baseSlot);
        return result;
    }

    // スロット割り当て: メロディ ch0-8 → 0-8、リズム5楽器 → 9-13
    static uint64_t keyChannelSlotMask(uint8_t reg, uint8_t changedBits) {
        constexpr uint8_t kRhythmReg = (TType == ChipType::OPL2EX) ? 0xbd : 0x0e;
        if (reg == kRhythmReg) return rhythmSlotMask(changedBits, 9u);
        return uint64_t{1} << (reg & 0x0fu);
    }

    MemoryYmfmInterface m_iface;
    ChipImpl           m_chip;
    uint32_t           m_clock;
    uint32_t           m_native_rate = 0;
    uint32_t           m_target_rate = 0;
    LinearResampler    m_resampler;
    std::array<uint8_t, 256> m_lastKeyRegValue{};
};

// =========================================================
//  name() 特殊化
// =========================================================
template<> inline const char* FmChipImpl<ymfm::y8960opl2ex, ChipType::OPL2EX>::name() const { return "OPL2EX"; }
template<> inline const char* FmChipImpl<ymfm::y8960opllex, ChipType::OPLLEX>::name() const { return "OPLLEX"; }

// =========================================================
//  コンストラクタ特殊化
//  Y8960拡張チップ2種はいずれも (ymfm_interface&) のみを取る
// =========================================================
template<>
inline FmChipImpl<ymfm::y8960opl2ex, ChipType::OPL2EX>::FmChipImpl(uint32_t clock)
    : m_chip(m_iface), m_clock(clock ? clock : FmClock::OPL2EX)
{ m_chip.reset(); m_native_rate = m_chip.sample_rate(m_clock); }

template<>
inline FmChipImpl<ymfm::y8960opllex, ChipType::OPLLEX>::FmChipImpl(uint32_t clock)
    : m_chip(m_iface), m_clock(clock ? clock : FmClock::OPLLEX)
{ m_chip.reset(); m_native_rate = m_chip.sample_rate(m_clock); }

// =========================================================
//  ファクトリ関数 (ChipType版)
// =========================================================
inline std::unique_ptr<FmChip> createChip(ChipType type, uint32_t clock = 0) {
    auto resolve = [](uint32_t c, uint32_t def) { return c ? c : def; };
    switch (type) {
        case ChipType::OPL2EX:
            return std::make_unique<FmChipImpl<ymfm::y8960opl2ex, ChipType::OPL2EX>>(resolve(clock, FmClock::OPL2EX));
        case ChipType::OPLLEX:
            return std::make_unique<FmChipImpl<ymfm::y8960opllex, ChipType::OPLLEX>>(resolve(clock, FmClock::OPLLEX));
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
        { "OPL2EX", ChipType::OPL2EX, FmClock::OPL2EX },
        { "OPLLEX", ChipType::OPLLEX, FmClock::OPLLEX },
        { nullptr,  ChipType::OPL2EX, 0               },  // sentinel
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
