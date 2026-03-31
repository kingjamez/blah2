#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define DLCR_CHANNEL_COUNT  8
#define DLCR_SERIAL_LEN     16
#define DLCR_SAMPLE_RATE    25e6

#ifdef __cplusplus
extern "C" {
#endif

struct dlcr;
typedef struct dlcr dlcr_t;

struct dlcr_info {
    char serial[DLCR_SERIAL_LEN+1];
};
typedef struct dlcr_info dlcr_info_t;

struct dlcr_dev_info {
    uint8_t hw_ver_major;
    uint8_t hw_ver_minor;
    uint8_t fw_ver_major;
    uint8_t fw_ver_minor;
    uint8_t fw_ver_build;
};
typedef struct dlcr_dev_info dlcr_dev_info_t;

enum dlcr_channel {
    DLCR_CHAN_NONE = 0x00,
    DLCR_CHAN_1    = (1 << 0),
    DLCR_CHAN_2    = (1 << 1),
    DLCR_CHAN_3    = (1 << 2),
    DLCR_CHAN_4    = (1 << 3),
    DLCR_CHAN_5    = (1 << 4),
    DLCR_CHAN_6    = (1 << 5),
    DLCR_CHAN_7    = (1 << 6),
    DLCR_CHAN_8    = (1 << 7),
    DLCR_CHAN_ALL  = 0xFF
};
typedef enum dlcr_channel dlcr_channel_t;

enum dlcr_clock {
    DLCR_CLOCK_INTERNAL = 0x00,
    DLCR_CLOCK_EXTERNAL = 0x01
};
typedef enum dlcr_clock dlcr_clock_t;

#pragma pack(push, 1)
struct dlcr_complex {
    float re;
    float im;
};
typedef struct dlcr_complex dlcr_complex_t;
#pragma pack(pop)

typedef void (*dlcr_callback_t)(dlcr_complex_t* samples[DLCR_CHANNEL_COUNT], size_t count, size_t drops, void* ctx);

int dlcr_list_devices(dlcr_info_t** devices);
void dlcr_free_device_list(dlcr_info_t* devices);
int dlcr_open(dlcr_t** dev, const char* serial);
void dlcr_close(dlcr_t* dev);
void dlcr_get_dev_info(dlcr_t* dev, dlcr_dev_info_t* info);
int dlcr_start(dlcr_t* dev, size_t buffer_size, dlcr_callback_t callback, void* ctx);
int dlcr_stop(dlcr_t* dev);
int dlcr_enable_channel(dlcr_t* dev, dlcr_channel_t channels);
int dlcr_disable_channel(dlcr_t* dev, dlcr_channel_t channels);
int dlcr_set_freq(dlcr_t* dev, dlcr_channel_t channels, double freq, bool calibrate);
int dlcr_set_lna_gain(dlcr_t* dev, dlcr_channel_t channels, int gain);
int dlcr_set_mixer_gain(dlcr_t* dev, dlcr_channel_t channels, int gain);
int dlcr_set_vga_gain(dlcr_t* dev, dlcr_channel_t channels, int gain);
int dlcr_set_gain(dlcr_t* dev, dlcr_channel_t channels, int gain);
int dlcr_set_clock_source(dlcr_t* dev, dlcr_clock_t clock);

#ifdef __cplusplus
}
#endif
