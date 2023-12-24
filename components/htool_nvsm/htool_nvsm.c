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
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "htool_nvsm.h"


static const int FLOAT_CONVERSION_FACTOR = 10000;

static const char *TAG = "htool_nvsm";

static bool init_done = false;

enum nvsm_types_t {
    NVSM_I8,
    NVSM_U8,
    NVSM_I16,
    NVSM_U16,
    NVSM_I32,
    NVSM_U32,
    NVSM_I64,
    NVSM_U64,
    NVSM_STR
};

// region NVS handling functions
static int open_nvs_handle(nvs_handle_t *handle) {
    esp_err_t err;
    if ((err = nvs_open("storage", NVS_READWRITE, handle)) != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return -1;
    }

    return 1;
}


static void close_nvs_handle(nvs_handle_t handle) {
    nvs_close(handle);
}


static void print_nvs_stats() {
    nvs_stats_t nvs_stats = {0};
    if (nvs_get_stats(NULL, &nvs_stats) == ESP_OK) {
        ESP_LOGI(TAG, "Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n", nvs_stats.used_entries,
                 nvs_stats.free_entries, nvs_stats.total_entries);
    }
}


static int set(enum nvsm_types_t type, const char *key, int8_t val_i8, uint8_t val_u8, int16_t val_i16, uint16_t
val_u16, int32_t val_i32, uint32_t val_u32, int64_t val_i64, uint64_t val_u64, const char *val_str) {
    int rc = 1;
    esp_err_t err;
    nvs_handle_t my_handle;

    if (!key) {
        return -2;
    }

    if (open_nvs_handle(&my_handle) > 0) {
        switch (type) {
            case NVSM_I8: {
                err = nvs_set_i8(my_handle, key, val_i8);
                break;
            }
            case NVSM_U8: {
                err = nvs_set_u8(my_handle, key, val_u8);
                break;
            }
            case NVSM_I16: {
                err = nvs_set_i16(my_handle, key, val_i16);
                break;
            }
            case NVSM_U16: {
                err = nvs_set_u16(my_handle, key, val_u16);
                break;
            }
            case NVSM_I32: {
                err = nvs_set_i32(my_handle, key, val_i32);
                break;
            }
            case NVSM_U32: {
                err = nvs_set_u32(my_handle, key, val_u32);
                break;
            }
            case NVSM_I64: {
                err = nvs_set_i64(my_handle, key, val_i64);
                break;
            }
            case NVSM_U64: {
                err = nvs_set_u64(my_handle, key, val_u64);
                break;
            }
            case NVSM_STR: {
                err = nvs_set_str(my_handle, key, val_str);
                ESP_LOGE(TAG, "set: %u", err);
                break;
            }
            default:
                err = ESP_ERR_NOT_SUPPORTED;
                break;
        }

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Cannot save value for key %s, Reason: %s", key, esp_err_to_name(err));
            rc = -1;
        }

        // Commit written value.
        // After setting any values, nvs_commit() must be called to ensure changes are written
        // to flash storage. Implementations may write to storage at other times,
        // but this is not guaranteed.
        if ((err = nvs_commit(my_handle)) != ESP_OK) {
            ESP_LOGE(TAG, "Error (%s) commit!", esp_err_to_name(err));
            rc = -1;
        }
        close_nvs_handle(my_handle);
    }

    return rc;
}


static int get(enum nvsm_types_t type, const char *key, int8_t *val_i8, uint8_t *val_u8, int16_t *val_i16, uint16_t
*val_u16, int32_t *val_i32, uint32_t *val_u32, int64_t *val_i64, uint64_t *val_u64, char *val_str, size_t
               *length) {
    int rc = 1;
    esp_err_t err;
    nvs_handle_t my_handle;

    if (!key) {
        return -2;
    }

    if (open_nvs_handle(&my_handle) > 0) {
        switch (type) {
            case NVSM_I8: {
                err = nvs_get_i8(my_handle, key, val_i8);
                break;
            }
            case NVSM_U8: {
                err = nvs_get_u8(my_handle, key, val_u8);
                break;
            }
            case NVSM_I16: {
                err = nvs_get_i16(my_handle, key, val_i16);
                break;
            }
            case NVSM_U16: {
                err = nvs_get_u16(my_handle, key, val_u16);
                break;
            }
            case NVSM_I32: {
                err = nvs_get_i32(my_handle, key, val_i32);
                break;
            }
            case NVSM_U32: {
                err = nvs_get_u32(my_handle, key, val_u32);
                break;
            }
            case NVSM_I64: {
                err = nvs_get_i64(my_handle, key, val_i64);
                break;
            }
            case NVSM_U64: {
                err = nvs_get_u64(my_handle, key, val_u64);
                break;
            }
            case NVSM_STR: {
                err = nvs_get_str(my_handle, key, val_str, length);
                break;
            }
            default:
                err = ESP_ERR_NOT_SUPPORTED;
                break;
        }

        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "NVS (%s) for %s!", esp_err_to_name(err), key);
            rc = -1;
        }
        else if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error (%s) for %s!", esp_err_to_name(err), key);
            rc = -1;
        }

        close_nvs_handle(my_handle);
    }

    return rc;
}
// endregion


// region Float conversion
static int float_to_i32(float value, int32_t *out_val) {
    value = value * (float)FLOAT_CONVERSION_FACTOR;
    *out_val = (int)value;
    return 1;
}


static int i32_to_float(int32_t value, float *out_val) {
    *out_val = (float)value / (float)FLOAT_CONVERSION_FACTOR;
    return 1;
}
// endregion


int nvsm_get_str(const char *key, char *out_value, size_t *length) {
    return get(NVSM_STR, key, 0, 0, 0, 0, 0, 0, 0, 0, out_value, length);
}


int nvsm_set_str(const char *key, const char *value) {
    return set(NVSM_STR, key, 0, 0, 0, 0, 0, 0, 0, 0, value);
}


int nvsm_get_u8(const char *key, uint8_t *out_value) {
    return get(NVSM_U8, key, 0, out_value, 0, 0, 0, 0, 0, 0, 0, 0);
}


int nvsm_set_u8(const char *key, uint8_t value) {
    return set(NVSM_U8, key, 0, value, 0, 0, 0, 0, 0, 0, 0);

}


int nvsm_get_i8(const char *key, int8_t *out_value) {
    return get(NVSM_I8, key, out_value, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}


int nvsm_set_i8(const char *key, int8_t value) {
    return set(NVSM_I8, key, value, 0, 0, 0, 0, 0, 0, 0, 0);
}


__attribute__((unused)) int nvsm_get_i16(const char *key, int16_t *out_value) {
    return get(NVSM_I16, key, 0, 0, out_value, 0, 0, 0, 0, 0, 0, 0);
}


__attribute__((unused)) int nvsm_set_i16(const char *key, int16_t value) {
    return set(NVSM_I16, key, 0, 0, value, 0, 0, 0, 0, 0, 0);
}


__attribute__((unused)) int nvsm_get_u16(const char *key, uint16_t *out_value) {
    return get(NVSM_U16, key, 0, 0, 0, out_value, 0, 0, 0, 0, 0, 0);
}


__attribute__((unused)) int nvsm_set_u16(const char *key, uint16_t value) {
    return set(NVSM_U16, key, 0, 0, 0, value, 0, 0, 0, 0, 0);
}


int nvsm_get_i32(const char *key, int32_t *out_value) {
    return get(NVSM_I32, key, 0, 0, 0, 0, out_value, 0, 0, 0, 0, 0);
}


int nvsm_set_i32(const char *key, int32_t value) {
    return set(NVSM_I32, key, 0, 0, 0, 0, value, 0, 0, 0, 0);
}


int nvsm_get_u32(const char *key, uint32_t *out_value) {
    return get(NVSM_U32, key, 0, 0, 0, 0, 0, out_value, 0, 0, 0, 0);
}


int nvsm_set_u32(const char *key, uint32_t value) {
    return set(NVSM_U32, key, 0, 0, 0, 0, 0, value, 0, 0, 0);
}


__attribute__((unused)) int nvsm_get_i64(const char *key, int64_t *out_value) {
    return get(NVSM_I64, key, 0, 0, 0, 0, 0, 0, out_value, 0, 0, 0);
}


__attribute__((unused)) int nvsm_set_i64(const char *key, int64_t value) {
    return set(NVSM_I64, key, 0, 0, 0, 0, 0, 0, value, 0, 0);
}


int nvsm_get_u64( const char *key, uint64_t *out_value) {
    return get(NVSM_U64, key, 0, 0, 0, 0, 0, 0, 0, out_value, 0, 0);
}


int nvsm_set_u64( const char *key, uint64_t value) {
    return set(NVSM_U64, key, 0, 0, 0, 0, 0, 0, 0, value, 0);
}


int nvsm_set_float(const char *key, float value) {
    int rc;
    int32_t cval;

    rc = float_to_i32(value, &cval);
    if (rc < 0) {
        ESP_LOGE(TAG, "Conversion error in float_to_i32! RC=%d", rc);
        return -1;
    }

    return set(NVSM_I32, key, 0, 0, 0, 0, cval, 0, 0, 0, 0);
}


int nvsm_get_float(const char *key, float *out_value) {
    int rc;
    int32_t value;

    if (get(NVSM_I32, key, 0, 0, 0, 0, &value, 0, 0, 0, 0, 0) < 0) {
        return -1;
    }

    if ((rc = i32_to_float(value, out_value)) < 0) {
        ESP_LOGE(TAG, "Conversion error in i32_to_float! RC=%d", rc);
        return rc;
    }

    return 1;
}


int nvsm_init() {
    if (!init_done) {
        ESP_LOGI (TAG, "Initializing nvs storage ...");

        // Initialize NVS
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGE(TAG, "Error initializing nvs! Error: %s", esp_err_to_name(err));

            // NVS partition was truncated and needs to be erased
            if ((err = nvs_flash_erase()) != ESP_OK) {
                ESP_LOGE(TAG, "Error erasing nvs storage! Error: %s", esp_err_to_name(err));
                return err;
            }

            // Retry nvs_flash_init
            err = nvs_flash_init();
        }

        // Print the current NVS stats
        print_nvs_stats();

        init_done = true;
        return err;
    }

    return ESP_OK;
}


int nvsm_deinit() {
    esp_err_t err;
    ESP_LOGD(TAG, "De-init default nvs partition");

    if ((err = nvs_flash_deinit()) != ESP_OK) {
        ESP_LOGE(TAG, "Error de-initializing nvs! Error: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    return 0;
}
