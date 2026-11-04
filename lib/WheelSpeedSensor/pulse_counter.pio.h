#pragma once
#include "hardware/pio.h"

// PIO-Programm (manuell kodiert, kein pioasm-Build-Step nötig):
//
//   .wrap_target
//   wait 0 pin 0    ; warte auf LOW  → verhindert Doppelzählung (Entprellung)
//   wait 1 pin 0    ; steigende Flanke erkannt
//   push noblock    ; Puls in RX-FIFO; nie blockieren (FIFO-Überlauf → Puls ignoriert)
//   .wrap
//
// Instruktions-Kodierung (RP2350 TRM, 16-Bit-Format):
//   wait 0 pin 0  → 001_00000 | 0_01_00000 = 0x2020
//   wait 1 pin 0  → 001_00000 | 1_01_00000 = 0x20A0
//   push noblock  → 100_00000 | 0_0_0_00000 = 0x8000  (IfFull=0, Block=0)

static const uint16_t pulse_counter_program_instructions[] = {
    0x2020u,  // wait 0 pin 0
    0x20A0u,  // wait 1 pin 0
    0x8000u,  // push noblock
};

static const struct pio_program pulse_counter_program = {
    .instructions = pulse_counter_program_instructions,
    .length       = 3,
    .origin       = -1,
};

// wrap_target = Instruktion 0, wrap = Instruktion 2
static inline pio_sm_config pulse_counter_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + 0u, offset + 2u);
    return c;
}
