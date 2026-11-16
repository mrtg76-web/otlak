// Zar: ses, algoritma ve pattern üretimi
#pragma once
#include "Otlak.h"
#include <random>

namespace otlak {

// Yeni ses (mutate=false) ya da küçük mutasyon (mutate=true). Kilitli parametrelere dokunmaz.
void rollSound(int voice, Params& p, int& algo, VoiceExtra& x, bool mutate, std::mt19937& rng);
// Zarla oluşmuş p-lock varyasyonlarını (tam kilit ya da kilitsiz 'o') yeni sese göre yeniden üret.
// Elle ayarlanmış kısmi kilitler korunur.
void regenVariants(Lane& lane, const Params& p, std::mt19937& rng);
StepLock randomVariant(const Params& p, std::mt19937& rng);
void rollPattern(Lane& lane, int voice, const Params& p, std::mt19937& rng);

} // namespace otlak
