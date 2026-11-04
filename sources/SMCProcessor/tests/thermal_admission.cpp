#include "../thermal_admission.hpp"
#include <cassert>
int main() {
    assert(raptorLakePThermal(true, 6, 0xBA, 0x41));
    assert(raptorLakePThermal(true, 6, 0xBA, 0xFFFFFFF7));
    // A model alone is insufficient: require Intel and both thermal features.
    assert(!raptorLakePThermal(false, 6, 0xBA, 0x41));
    assert(!raptorLakePThermal(true, 0xF, 0xBA, 0x41));
    for (uint32_t model = 0; model < 256; ++model)
        assert(raptorLakePThermal(true, 6, model, 0x41) == (model == 0xBA));
    assert(!raptorLakePThermal(true, 6, 0xBA, 0));
    assert(!raptorLakePThermal(true, 6, 0xBA, 1));
    assert(!raptorLakePThermal(true, 6, 0xBA, 0x40));
    return 0;
}
