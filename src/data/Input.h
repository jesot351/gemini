#pragma once

#include "managers/Platform.h"

#include <emmintrin.h> // SSE2
#include <smmintrin.h> // SSE4.1

#include <GLFW/glfw3.h>

enum KEY_STATE
{
    KEY_RELEASE    = GLFW_RELEASE,
    KEY_PRESS      = GLFW_PRESS,
    KEY_REPEAT     = GLFW_REPEAT,
    KEY_DOWN       = 3,
    NUM_KEY_STATES = 4,
};

extern ALIGN(16) uint64_t key_states[NUM_KEY_STATES][2]; // 16b packed key events

inline uint32_t key_pressed(uint32_t key)
{
    __m128i s = _mm_load_si128((__m128i*) &key_states[KEY_PRESS][0]);
    __m128i k = _mm_set1_epi16(key);
    __m128i c = _mm_cmpeq_epi16(s, k);

    return _mm_movemask_epi8(c);
}

inline uint32_t key_released(uint32_t key)
{
    __m128i s = _mm_load_si128((__m128i*) &key_states[KEY_RELEASE][0]);
    __m128i k = _mm_set1_epi16(key);
    __m128i c = _mm_cmpeq_epi16(s, k);

    return _mm_movemask_epi8(c);
}

inline uint32_t key_repeating(uint32_t key)
{
    uint16_t* repeating = (uint16_t*) &key_states[KEY_REPEAT][0];
    return (uint32_t) repeating[0] == key;
}

inline uint32_t key_down(uint32_t key)
{
    __m128i s = _mm_load_si128((__m128i*) &key_states[KEY_DOWN][0]);
    __m128i k = _mm_set1_epi16(key);
    __m128i c = _mm_cmpeq_epi16(s, k);

    return _mm_movemask_epi8(c);
}
