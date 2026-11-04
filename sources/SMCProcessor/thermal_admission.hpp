#pragma once
#include <stdint.h>

// Intel family 6 model BA is mobile Raptor Lake-P. Require the CPUID.06H
// core DTS and package thermal capabilities before using their read-only MSRs.
// An unknown generation on any other physical model remains unsupported.
constexpr bool raptorLakePThermal(bool intel, uint32_t family,
                                  uint32_t model, uint32_t thermalFeatures) {
    return intel && family == 6 && model == 0xBA &&
        (thermalFeatures & 0x41U) == 0x41U;
}
