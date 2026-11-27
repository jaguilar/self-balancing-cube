#pragma once

namespace stfoc {

/// Enables the STM32C5 CORDIC peripheral clock and configures it for Q1.31
/// cosine+sine evaluation (6-cycle precision, 1 write, 2 reads).
void InitCordicTrig();

}  // namespace stfoc
