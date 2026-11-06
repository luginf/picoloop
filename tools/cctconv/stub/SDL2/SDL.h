/* Minimal SDL stub: lets the converter compile picoloop's pattern I/O code
   (PatternReader, Pattern, PatternElement, SongSequencer) without SDL. */
#ifndef CCTCONV_SDL_STUB_H
#define CCTCONV_SDL_STUB_H
#include <stdint.h>
typedef int8_t Sint8; typedef uint8_t Uint8;
typedef int16_t Sint16; typedef uint16_t Uint16;
typedef int32_t Sint32; typedef uint32_t Uint32;
typedef int64_t Sint64; typedef uint64_t Uint64;
#define AUDIO_S16 0x8010
#endif
