#pragma once
#include "cpu_state.h"
#include <cstddef>
#include <cstdint>

/* Execute a bounded original cartridge subroutine over an explicit bus.
 * This is an adapter for verified mechanics/render routines, never another
 * running console. The caller owns and classifies every readable/writable
 * region. No device clocks, live guest memory, input sampling or presentation
 * are implicit. A rejected bus access fails the entire call. */
struct ZrRoutineBus {
    void* context;
    bool (*read)(void*, uint32_t address, unsigned width, uint32_t* value);
    bool (*write)(void*, uint32_t address, unsigned width, uint32_t value);
};
using ZrNativeCall = void (*)(CPUState*, uint32_t entry, uint32_t return_pc);
struct ZrRoutineResult {
    bool completed;
    uint32_t failure_pc, failure_address;
    uint64_t cycles;
};
/* Supply the generated vb_dispatch_call for native execution. A null native
 * callback uses the portable V810 interpreter, under the same bus contract.
 * cpu is caller-owned scratch. Calls are serialized on the guest thread;
 * nesting is rejected. The live execution/debugger control state is retained. */
ZrRoutineResult ZrRoutineRun(CPUState& cpu, const ZrRoutineBus& bus,
                            uint32_t entry, uint32_t return_pc,
                            uint64_t cycle_limit, ZrNativeCall native = nullptr);
