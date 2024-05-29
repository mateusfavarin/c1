#include "audio.h"
#include "midi.h"
#include "globals.h"
#include "math.h"
#include "gool.h"
#include "level.h"
#include "pc/gfx/soft.h"

typedef struct {
  int left;
  int right;
} Volume;

typedef struct {
  uint32_t flags;
  uint32_t unknown;
  gool_object *obj;
  int8_t delay_counter;
  int8_t sustain_counter;
  int16_t amplitude;
  uint16_t case7val;
  int16_t pitch;
  vec r_trans;
  vec trans;
  int16_t tgt_amplitude;
  int16_t tgt_pitch;
  int32_t ramp_counter;
  int32_t ramp_step;
  int32_t glide_counter;
  int32_t glide_step;
} audio_voice_params;

typedef struct {
  int id;
  audio_voice_params params;
} audio_voice;

/* .sbss */
int voice_id_ctr;                /* 8005663C; gp[0x90] */
Volume spatial_vol;              /* 80056640; gp[0x91] */
audio_voice voices[24];          /* 80056804 */
uint8_t keys_status[24];            /* 80056E64 */
audio_voice_params voice_params; /* 80056E7C */

#define master_voice voices[23]

extern ns_struct ns;
extern lid_t cur_lid;
extern entry *cur_zone;

extern int32_t ramp_rate;
extern uint32_t init_vol;
extern int32_t fade_vol;
extern int32_t fade_vol_step;
extern int max_midi_voices;
extern uint32_t seq2_vol;

/* note: return types for AudioInit and AudioKill are void in orig impl;
 *       made int for subsys map compatibility */
//----- (8002FDE0) --------------------------------------------------------
int AudioInit() {
  int reverb_mode, i;

  for (i=0;i<24;i++)
    voices[i].params.flags &= ~8;

  SwAudioInit();
  /* route midi audio to voice 0 */
  SwVoiceSetCallback(0, SwMidiProcess);
  /* set gain to that of 8 simultaneously sounded voices */
  SwVoiceSetGain(0, 8.0f);
  /* set inital values for master voice */
  master_voice.params.delay_counter = 1;
  master_voice.params.sustain_counter = 128u;
  master_voice.params.amplitude = 0x3FFF;
  master_voice.params.pitch = 0x1000;
  master_voice.params.obj = 0;
  master_voice.params.case7val = 0;
  master_voice.params.r_trans.x = 0;
  master_voice.params.r_trans.y = 0;
  master_voice.params.r_trans.z = 0;
  master_voice.params.flags = (voice_params.flags & 0xFFFFF000) | 0x600;
  /* set reverb mode for levels with reverb */
  switch (cur_lid) {
  case LID_GENERATORROOM:
  case LID_TEMPLERUINS:
  case LID_JAWSOFDARKNESS:
  case LID_BONUSCORTEX:
    reverb_mode = 5;
    break;
  case LID_BONUSBRIO:
    reverb_mode = 3;
    break;
  default:
    reverb_mode = 0;
  }
  if (reverb_mode)
    AudioSetReverbAttr(reverb_mode, 0x2000, 0x2000, 0, 0);
  /* set init_vol (volume for voices) as sfx volume */
  AudioSetVoiceMVol((uint32_t)(0x3FFF00 * sfx_vol) >> 16);
  fade_vol = 0x3FFF;
  fade_vol_step = 0;
  seq2_vol = 0;
  /* finally set master volume */
  SwSetMVol(127);
  return SUCCESS;
}

//----- (8002FFC0) --------------------------------------------------------
int AudioKill() {
  SwAudioKill();
  return SUCCESS;
}

//----- (8002FFFC) --------------------------------------------------------
void AudioSetVoiceMVol(uint32_t vol) {
  init_vol = vol;
}

//----- (80030008) --------------------------------------------------------
int AudioSetReverbAttr(int mode, int16_t depth_left, int16_t depth_right, int delay, int feedback) {
  return 0;
}

//----- (80030078) --------------------------------------------------------
static Volume AudioSpatialize(vec *v, int vol) {
  lid_t lid;
  vec va;
  uint32_t mag, amp;
  int32_t ang_xz;
  uint16_t left, right;

  va.x = v->x / 32; /* 3 or 15 bit frac fixed point */
  va.y = v->y / 32;
  va.z = (v->z-(1200<<8)) / 32;
  lid = ns.ldat->lid;
  /* orig impl had a left-over unused call to SqrMagnitude2 in the first if block */
  if (lid == LID_STORMYASCENT || lid == LID_SLIPPERYCLIMB)
    mag = SwSqrMagnitude3(va.x, va.z, va.y);
  else
    mag = SwSqrMagnitude2(va.x, va.z);

  if (mag > (32<<10)-1) {
    left = (vol << 15) / mag; /* 5bffp or 17bffp... */
    right = left;
    if (!mono) {
      ang_xz = atan2(va.z, va.x);
      amp = cos(ang_xz) + 0x1000; /* cos in (0-2) range */
      /* 5bffp * 12bffp = 17bffp; 17bffp >> 11 = 7bffp / 2
         i.e. left = left*(1 + (2.0-amp))/2 */
      left = (left + left*(0x2000 - amp)) >> 11;
      right = (left + left*amp) >> 11;
    }
    left = limit(left, 0, vol);
    right = limit(right, 0, vol);
    spatial_vol.left = left;
    spatial_vol.right = right;
  }
  else {
    spatial_vol.left = vol;
    spatial_vol.right = vol;
  }
  return spatial_vol;
}

//----- (80030260) --------------------------------------------------------
void AudioVoiceFree(gool_object *obj) {
  audio_voice *voice;
  generic res;
  int i, count;

  if (!obj) { return; }
  count = max_midi_voices;
  for (i=0;i<24;i++) {
    voice = &voices[i];
    if ((voice->params.flags & 8) && voice->params.obj == obj) {
      ramp_rate = 9;
      AudioControl(voice->params.flags, 0x40000000, &res, voice->params.obj);
      voice->params.obj = 0;
      voice->params.sustain_counter = 0;
    }
  }
}

/**
 * allocate an audio voice
 *
 * the function tries for the first free/unused (flag bit 4 clear) voice.
 * if no free voice is found it then tries for the voice with the shortest
 * remaining lifetime (the one that will sound off soonest). if multiple
 * voices have this particular remaining amount then it tries for the
 * quietest one.
 *
 * the following cases prevent the function from otherwise stealing from
 * voices that have not yet sounded:
 * - if the shortest remaining lifetime is longer than the default initial
 *   value then the function will return error (-1)
 * - if the shortest remaining lifetime is equal to the default initial
 *   value and the quietest voice amongst those that have that remaining
 *   lifetime is louder than 12.5% of full volume, then the function has a
 *   50/50 chance of not allocating a voice and instead returning an error
 */
//----- (80030328) --------------------------------------------------------
int AudioVoiceAlloc() {
  audio_voice *voice;
  uint8_t min_ttl; /* minimum number of iterations until a voice is freed */
  uint16_t min_vol;
  uint16_t left, right;
  int i, idx_min;

  min_ttl = 255;
  min_vol = 0x3FFF;
  for (i=max_midi_voices;i<24;i++) { /* find a free voice (has flag bit 4 clear) and return its idx */
    voice = &voices[i];
    if (voice->params.sustain_counter < min_ttl)
      min_ttl = voice->params.sustain_counter; /* also keep track of shortest remaining lifetime */
    if (!(voice->params.flags & 8)) /* free voice? */
      return i; /* return the index */
  }
  /* no such voice was found if this point is reached */
  if (min_ttl > voice_params.sustain_counter) { return -1; } /* return error if min rem. lifetime too high */
  for (i=max_midi_voices;i<24;i++) { /* if more than one voice has the shortest remaining lifetime, find the quietest one */
    voice = &voices[i];
    if (voice->params.sustain_counter == min_ttl) {
      Volume volume;
      if (voice->params.obj && (voice->params.obj->process.status_b & 0x200)) { /* no spatialization? */
        volume.left  = voice->params.amplitude;
        volume.right = voice->params.amplitude;
        voice->params.flags &= ~0x200;
      }
      else { volume = AudioSpatialize(&voice->params.r_trans, voice->params.amplitude); }

      left=abs(volume.left);
      right=abs(volume.right);
      if (min(left, right) < min_vol) {
        min_vol = min(left, right);
        idx_min = i;
      }
    }
  }
  /* master voice has shortest rem. lifetime and that voice is loud? */
  if (min_ttl == voice_params.sustain_counter && min_vol >= 0x800) {
    if (randb(100) >= 50) { return -1; } /* possibly return error */
  }
  return idx_min; /* return idx of shortest rem. lifetime voice (or the quietest of them if multiple) */
}

/**
 * create an audio voice for the given object and wavebank (entry reference or address),
 * with the given volume.
 *
 * the voice amplitude is set as vol times voice_vol (the voice master volume)
 * the voice obj is set as obj, and if obj is nonzero, the voice trans is set as obj trans
 * all other parameters for the voice are initialized from 'voice_params', which can
 * be preset with AudioControl operations on voice id 0. (note that the 'voice_params'
 * are reset back to the default values after this function runs, so AudioControl
 * must again be used to set the values for the next voice created, and so on, if
 * any values other than the defaults are desired.)
 *
 * a lower level VoiceAttr object is created for the voice, the volume of which is
 * set to the 'spatialized' voice amplitude, which is based on distance of the object
 * from camera and scalar projection in the XZ plane. (if spatialization is disabled
 * volume values for both channels are set directly to the voice amplitude). the attr
 * is also setup for reverb if reverb flag is set. key on is immediately set for the
 * voice if bit 5 is set in the initial flags.
 *
 * the function returns the newly allocated voice id for the new voice.
 */
//----- (800304C8) --------------------------------------------------------
int AudioVoiceCreate(gool_object *obj, eid_t *eid, int vol) {
  Volume volume;
  entry *adio;
  size_t size;
  audio_voice *voice;
  int idx;

  if (!sfx_vol) { return 0; } /* return if sfx are muted */
  idx = AudioVoiceAlloc(); /* try to allocate a voice */
  if (idx <= 0) { return -1; } /* return error on failure to allocate */
  voice = &voices[idx]; /* get the allocated voice */
  adio = NSLookup(eid);
  size = adio->items[1]-adio->items[0];
  SwLoadSample(idx, adio->items[0], size);
  voice->params = voice_params;
  voice->params.amplitude = (vol*init_vol) >> 14;
  if (voice->params.flags & 0x40)
    voice->params.ramp_step = (voice->params.tgt_amplitude-voice->params.amplitude)/voice->params.ramp_counter;
  voice->params.obj = obj;
  if (obj) {
    voice->params.trans = obj->process.vectors.trans;
    GoolTransform2(&obj->process.vectors.trans, &voice->params.r_trans, 1);
  }
  /* reset voice_params to the defaults in case they were changed by AudioControl */
  voice_params.delay_counter = 1;
  voice_params.sustain_counter = 128u;
  voice_params.amplitude = 0x3FFF;
  voice_params.pitch = 0x1000;
  voice_params.obj = 0;
  voice_params.case7val = 0;
  voice_params.r_trans.x = 0;
  voice_params.r_trans.y = 0;
  voice_params.r_trans.z = 0;
  voice_params.flags = (voice_params.flags & 0xFFFFF000) | 0x600;
  /* bugfix: orig impl did not test voice->obj before accessing it here */
  if (voice->params.obj && voice->params.obj->process.status_b & 0x200) { /* no spatialization? */
    volume.left  = voice->params.amplitude;
    volume.right = voice->params.amplitude;
    voice->params.flags &= ~0x200;
  }
  else { volume = AudioSpatialize(&voice->params.r_trans, voice->params.amplitude); }

  /* non-delayed voice? */
  if (!(voice->params.flags & 0x10)) { SwNoteOn(idx); } /* turn key on immediately */

  SwVoiceSetVolume(idx, volume.left, volume.right);
  SwVoiceSetPitch(idx, voice->params.pitch);
  voice->id = ++voice_id_ctr; /* allocate next id for the voice */
  voice->params.flags |= 8; /* set 'used' flag */
  return voice_id_ctr; /* return the voice id */
}

/**
 * controls a single audio voice or set of audio voices for an object
 *
 * to control a single voice, set id to the voice id
 * to control all voice(s) for a particular object,
 * set id to -1 and set obj to the object
 * to control the initial params for the next voice created, set id to 0
 *
 * the lower 3 bytes of op should be one of the following control ops,
 * and arg should be set to a value of the corresponding type:
 *
 * | op | description                | arg description  | arg type     |
 * |----|----------------------------|------------------|--------------|
 * | 0  | set amplitude              | amplitude        | signed int   |
 * | 1  | set pitch                  | pitch            | signed int   |
 * | 2  | set location [emittance pt]| location         | vector       |
 * | 3  | set location (pre-rotated) | rotated location | vector       |
 * | 4  | set delay amount           | delay amount     | signed byte  |
 * | 5  | set object                 | object           | object       |
 * | 6  | set glide/ramp rate        | glide/ramp rate  | signed int   |
 * | 7  | trigger key on             | case7val?        | unsigned int |
 * | 8  | enable spatialization      | n/a              | n/a          |
 * | 9  | disable spatialization     | n/a              | n/a          |
 * | 10 |                            | flag << 8        | unsigned int |
 * | 11 | enable/disable reverb      | flag << 8        | unsigned int |
 * | 12 | set sustain amount         | sustain amount   | signed byte  |
 *
 * the upper byte of op can include the following control flags:
 *
 * bit 6 - enable ramp or glide, when op 0 or 1 are used
 *         ramp will occur from current amplitude to target amplitude
 *         glide will occur from current pitch to target pitch
 * bit 7 - force key off at end of glide/ramp
 * bit 8 - force key off
 *
 * id    - id of the voice to control
 *         -1 if controlling all voices for an object
 *          0 if controlling initial params for next voice created
 * op    - op to perform (lower 3 bytes) and control flags (upper byte) (see above)
 * arg   - control argument
 * obj   - object with voice(s) to control, if id == -1
 */
//----- (80030840) --------------------------------------------------------
void AudioControl(int id, int op, generic *arg, gool_object *obj) {
  Volume volume;
  audio_voice *voice; // $s3
  audio_voice_params *params; // $s0
  vec v; // [sp+90h] [-28h] BYREF
  int i;

  for (i=max_midi_voices;i<24;i++) {
    if (id && ((id == -1 && voices[i].params.obj != obj) /* id is -1 and voice i does not have the specified obj? */
      || voices[i].id != id)) /* ...or id is nonzero and voice i does not have this id? */
      continue; /* skip the voice */
    voice = (audio_voice*)((uint8_t*)&voice_params-sizeof(uint32_t)); /* cur voice */
    if (id)
      voice = &voices[i];
    if (op & 0x80000000) { voice->params.flags |= 1; } /* set flag for 'force off'                    if bit 32 is set */
    if (op & 0x40000000) { voice->params.flags |= 2; } /* set flag for 'force off when flag clear'    if bit 31 is set */
    if (op & 0x20000000) { voice->params.flags |= 4; } /* set flag for 'amplitude ramp/glide enabled' if bit 30 is set */

    switch (op & 0xFFFFFFF) {
    case 0: /* set amplitude */
      if (voice->params.flags & 4) { /* amplitude ramp enabled? */
        voice->params.tgt_amplitude = arg->s32;
        voice->params.ramp_counter = ramp_rate;
        if (id) /* single voice control mode? */
          voice->params.ramp_step = (voice->params.tgt_amplitude - voice->params.amplitude) / ramp_rate;
        voice->params.flags |= 0x40; /* set 'ramping' status */
      }
      else {
        voice->params.amplitude = arg->s32;
        if (id) { /* single voice control mode? */
          volume = AudioSpatialize(&voice->params.r_trans, voice->params.amplitude);
          SwVoiceSetVolume(i, volume.left, volume.right);
        }
      }
      break;
    case 1: /* set pitch */
      if (voice->params.flags & 4) { /* glide/portamento enabled? */
        voice->params.tgt_pitch = arg->s32;
        voice->params.glide_counter = ramp_rate; /* calculate counter */
        voice->params.glide_step = (voice->params.tgt_pitch - voice->params.pitch) / ramp_rate; /* calculate step */
        voice->params.flags |= 0x80; /* set 'gliding' status */
      }
      else {
        voice->params.pitch = arg->s32; /* set pitch directly */
        if (id) { /* single voice control mode? */
          SwVoiceSetPitch(i, voice->params.pitch);
        }
      }
      break;
    case 2: /* set voice location */
      v = arg->v;
      GoolTransform2(&v, &arg->v, 1);
      /* fall through!!! */
    case 3: /* set voice location (pre-rotated) */
      if (voice->params.flags & 4) { break; } /* skip if amplitude ramp or portamento enabled */
      voice->params.r_trans = arg->v;
      if (id) {
        voice->params.r_trans = arg->v;
        volume = AudioSpatialize(&voice->params.r_trans, voice->params.amplitude);
        SwVoiceSetVolume(i, volume.left, volume.right);
      }
      break;
    case 4: /* set delay amount */
      voice->params.delay_counter = arg->s8;
      break;
    case 5: /* set voice object */
      voice->params.obj = arg->o;
      break;
    case 6: /* set glide/ramp rate */
      ramp_rate = arg->s32 ? arg->s32 : 1;
      break;
    case 7: /* delay voice */
      voice->params.case7val = arg->u32;
      voice->params.flags |= 0x10;
      break;
    case 8: /* set as an object voice */
      voice->params.flags |= 0x200;
      break;
    case 9: /* unset as an object voice */
      voice->params.flags &= ~0x200;
      break;
    case 10:
      voice->params.flags = (voice->params.flags & 0xFFFFF7FF) | ((arg->u32<<3)&0x800);
      break;
    case 11: /* enable reverb */
      voice->params.flags = (voice->params.flags & 0xFFFFFBFF) | ((arg->u32<<2)&0x400);
      break;
    case 12: /* set sustain amount */
      voice->params.sustain_counter = arg->s8;
      break;
    default:
      break;
    }
    if (id != -1) { return; } /* quit if not controlling [other] voices for a single object */
  }
}

/**
 * update all voices
 *
 * this includes decrementing all counters, incl. sustain counters,
 * delay counters, ramp counters, and glide counters, keeping
 * voices on while they sustain (sustain counter > 0), incrementing/
 * decrementing amplitude and/or pitch while they ramp/glide, delaying
 * the key on event while they delay, and clearing corresponding flags
 * when target counter values are reached. in particular, a voice key
 * is turned off and the voice is freed when the sustain/'remaining
 * lifetime' reaches zero, or when it is forced off
 *
 * all updated values are updated as attributes in the corresponding
 * lower level VoiceAttr object for a particular voice.
 */
//----- (80030CC0) --------------------------------------------------------
void AudioUpdate() {
  Volume volume;
  audio_voice *voice;
  zone_header *header;
  uint16_t vol;
  int i, flag;

  flag = 0;
  /* reset volumes if title */
  if (ns.ldat->lid == LID_TITLE) {
    init_vol = (0x3FFF * sfx_vol) >> 8;
    vol = (0x3FFF00 * mus_vol) >> 16;
    MidiResetSeqVol(vol);
  }
  /* update midi */
  if (cur_zone) {
    header = (zone_header*)cur_zone->items[0];
    MidiUpdate(&header->gfx.midi); /* 0x304 */
  }
  if (fade_vol_step) { /* volume fading? */
    if (fade_vol_step < 0 && fade_vol < abs(fade_vol_step)) { /* fading out and fade_vol < abs(fade_vol_step)? */
      fade_vol = 0; /* lower limit at 0 */
      fade_vol_step = 0; /* stop fading */
    }
    if (fade_vol_step > 0 && 0x3FFF - fade_vol < fade_vol_step) { /* fading in and fade_vol > fade_vol - fade_vol_step ? */
      fade_vol = 0x3FFF; /* upper limit fade_vol at 0x3FFF */
      fade_vol_step = 0; /* stop fading */
    }
    fade_vol += fade_vol_step; /* increase/decrease volume */
    SwSetMVol(fade_vol);
  }
  #define KEYS_STATUS_ON 1
  SwGetAllKeysStatus(keys_status);
  flag=0;
  for (i=max_midi_voices;i<24;i++) {
    voice = &voices[i];
    if (!(voice->params.flags & 8)) { continue; } /* skip inactive/free voices */
    if (voice->params.flags & 0x10) { /* delayed voice? */
      if (--voice->params.delay_counter == 0) { /* decrement delay; has countdown finished? */
        voice->params.flags &= ~0x10; /* clear key triggered status */
        SwNoteOn(i);
      }
    }
    if (keys_status[i] == KEYS_STATUS_ON) {
      if (--voice->params.sustain_counter != 0) {/* decrement sustain; still sustaining? */

      }
      else {
        SwNoteOff(i);
        voice->params.flags &= ~8; /* and free up the voice */
        /*
        if (voice->flags & 1 || (!flag && voice->flags & 2)) {
          SpuSetKey(SPU_OFF, 1<<i);
          voice->flags &= ~8;
        }
        */
        continue;
      }
    }
    else {
      voice->params.flags &= ~8;
    }
    if (voice->params.flags & 0x40) { /* currently ramping (amplitude)? */
      voice->params.amplitude += voice->params.ramp_step; /* increase amplitude */
      if (--voice->params.ramp_counter > 0) /* not done ramping? */
        flag = 1; /* set flag */
      else
        voice->params.flags &= ~0x40; /* else clear currently ramping flag */
      /* bugfix: orig impl did not test voice->obj before accessing it here */
      if (voice->params.obj && (voice->params.obj->process.status_b & 0x200)) { /* no spatialization? */
        volume.left = voice->params.amplitude;  /* set volume directly to the amplitude */
        volume.right = voice->params.amplitude;
      }
      else { volume = AudioSpatialize(&voice->params.r_trans, voice->params.amplitude); } /* else spatialize */
      SwVoiceSetVolume(i, volume.left, volume.right);
    }
    if (voice->params.flags & 0x80) { /* currently gliding? */
      voice->params.pitch += voice->params.glide_step; /* increase pitch */
      if (--voice->params.glide_counter > 0) /* not done gliding? */
        flag = 1; /* set flag */
      else
        voice->params.flags &= ~0x80; /* else clear currenly gliding flag */

      SwVoiceSetPitch(i, voice->params.pitch);
    }
    if ((voice->params.flags & 0x200) && voice->params.obj) { /* voice emitted from object? */
      voice->params.trans = voice->params.obj->process.vectors.trans; /* set to object trans */
      GoolTransform2(&voice->params.trans, &voice->params.r_trans, 1); /* trans, rotate, and scale */
      volume = AudioSpatialize(&voice->params.r_trans, voice->params.amplitude); /* spatialize w.r.t. object */
      SwVoiceSetVolume(i, volume.left, volume.right);
    }

    if ((voice->params.flags & 1) || (!flag && (voice->params.flags & 2))) { /* forced off? */
      SwNoteOff(i);
      voice->params.flags &= ~8; /* free up voice */
    }
  }
}
