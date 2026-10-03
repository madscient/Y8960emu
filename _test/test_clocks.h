// test_clocks.h
// テストがチップに渡すクロック (Hz)。エンジンは既定のクロックを持たないので、
// テストが渡す。テストの期待値 (発音の高さ、ネイティブレート) はこの値を
// 前提にしている。
#pragma once

#include <cstdint>

// MSX のマスタークロック
constexpr uint32_t kTestClock = 3'579'545;
