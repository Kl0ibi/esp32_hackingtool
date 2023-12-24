/*
Copyright (c) 2023 kl0ibi

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */
int nvsm_get_str(const char *key, char *out_value, size_t *length);


int nvsm_set_str(const char *key, const char *value);


int nvsm_get_u8(const char *key, uint8_t *out_value);


int nvsm_set_u8(const char *key, uint8_t value);


int nvsm_get_i8(const char *key, int8_t *out_value);


int nvsm_set_i8(const char *key, int8_t value);


__attribute__((unused)) __attribute__((unused)) int nvsm_get_i16(const char *key, int16_t *out_value);


__attribute__((unused)) __attribute__((unused)) int nvsm_set_i16(const char *key, int16_t value);


__attribute__((unused)) __attribute__((unused)) int nvsm_get_u16(const char *key, uint16_t *out_value);


__attribute__((unused)) __attribute__((unused)) int nvsm_set_u16(const char *key, uint16_t value);


int nvsm_get_i32(const char *key, int32_t *out_value);


int nvsm_set_i32(const char *key, int32_t value);


int nvsm_get_u32(const char *key, uint32_t *out_value);


int nvsm_set_u32(const char *key, uint32_t value);


__attribute__((unused)) __attribute__((unused)) int nvsm_get_i64(const char *key, int64_t *out_value);


__attribute__((unused)) __attribute__((unused)) int nvsm_set_i64(const char *key, int64_t value);


__attribute__((unused)) int nvsm_set_i64(const char *key, int64_t value);


int nvsm_get_u64( const char *key, uint64_t *out_value);


int nvsm_set_u64( const char *key, uint64_t value);


int nvsm_set_float(const char *key, float value);


int nvsm_get_float(const char *key, float *out_value);


int nvsm_init();


int nvsm_deinit();
