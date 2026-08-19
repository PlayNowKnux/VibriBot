#ifndef R5900_FLOAT_H
#define R5900_FLOAT_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
uint32_t ee_f32_bits(float v);
float ee_bits_f32(uint32_t b);
uint32_t ee_fpu_input(uint32_t b);
uint32_t ee_cvt_s_w(int32_t v);
int32_t ee_cvt_w_s(uint32_t a);
uint32_t ee_mul_s_chop(uint32_t a, uint32_t b);
uint32_t ee_add_s_chop(uint32_t a, uint32_t b);
uint32_t ee_div_s_chop(uint32_t a, uint32_t b);
uint32_t ee_div_s_nearest(uint32_t a, uint32_t b);
uint32_t ee_sin_s_nearest(uint32_t a);
uint32_t ee_cos_s_nearest(uint32_t a);
#ifdef __cplusplus
}
#endif
#endif
