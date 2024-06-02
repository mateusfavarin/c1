#include <SDL2/SDL.h>
#include "pc/pad.h"

extern uint8_t keys[512];

typedef struct {
  SDL_Keycode code;
  uint32_t bits;
} sw_pad_mapping;

const sw_pad_mapping pad_mappings[] = {
  { .code = SDLK_SPACE,      .bits = PAD_SELECT   },
  { .code = SDLK_1,          .bits = PAD_L3       },
  { .code = SDLK_3,          .bits = PAD_R3       },
  { .code = SDLK_RETURN,     .bits = PAD_START    },
  { .code = SDLK_UP+0x80,    .bits = PAD_UP       },
  { .code = SDLK_RIGHT+0x80, .bits = PAD_RIGHT    },
  { .code = SDLK_DOWN+0x80,  .bits = PAD_DOWN     },
  { .code = SDLK_LEFT+0x80,  .bits = PAD_LEFT     },
  { .code = SDLK_q,          .bits = PAD_L2       },
  { .code = SDLK_e,          .bits = PAD_R2       },
  { .code = SDLK_a,          .bits = PAD_L1       },
  { .code = SDLK_d,          .bits = PAD_R1       },
  { .code = SDLK_s,          .bits = PAD_TRIANGLE },
  { .code = SDLK_c,          .bits = PAD_CIRCLE   },
  { .code = SDLK_z,          .bits = PAD_CROSS    },
  { .code = SDLK_x,          .bits = PAD_SQUARE   },
};

const sw_pad_mapping joy_mappings[] = {
  { .code = 0,  .bits = PAD_CROSS    },
  { .code = 1,  .bits = PAD_CIRCLE   },
  { .code = 2,  .bits = PAD_SQUARE   },
  { .code = 3,  .bits = PAD_TRIANGLE },
  { .code = 4,  .bits = PAD_SELECT   },
  { .code = 6,  .bits = PAD_START    },
  { .code = 9,  .bits = PAD_L1       },
  { .code = 10, .bits = PAD_R1       },
  { .code = 11, .bits = PAD_UP       },
  { .code = 12, .bits = PAD_DOWN     },
  { .code = 13, .bits = PAD_LEFT     },
  { .code = 14, .bits = PAD_RIGHT    },
};

uint32_t SwPadRead(int idx) {
  uint32_t held, bits;
  int i, count, code;

  held = 0;
  if (idx != 0) { return held; } /* pad 1 only for now */
  count = arr_len(pad_mappings);
  for (i = 0; i < count; i++) {
    code = pad_mappings[i].code;
    if (keys[code & 0xFF]) {
      bits = pad_mappings[i].bits;
      held |= bits;
    }
  }
  if (gGameController) {
    count = arr_len(joy_mappings);
    for (i = 0; i < count; i++) {
      code = joy_mappings[i].code;
      if (SDL_JoystickGetButton(gGameController, code)) {
        bits = joy_mappings[i].bits;
        held |= bits;
      }
    }
  }
  return (held << 16);
}
