/*
 * Airspy backend
 *
 * Copyright  2014-2017  Alexandru Csete OZ9AEC
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/datatypes.h"
#include "common/library_loader.h"
#include "common/ring_buffer_cplx.h"
#include "common/sdr_data.h"
#include "common/time.h"
#include "sdr_device.h"


// Airspy API definitions
#define AIRSPY_VER_MAJOR 1
#define AIRSPY_VER_MINOR 0

enum airspy_error
{
	AIRSPY_SUCCESS = 0,
	AIRSPY_TRUE = 1,
	AIRSPY_ERROR_INVALID_PARAM = -2,
	AIRSPY_ERROR_NOT_FOUND = -5,
	AIRSPY_ERROR_BUSY = -6,
	AIRSPY_ERROR_NO_MEM = -11,
	AIRSPY_ERROR_LIBUSB = -1000,
	AIRSPY_ERROR_THREAD = -1001,
	AIRSPY_ERROR_STREAMING_THREAD_ERR = -1002,
	AIRSPY_ERROR_STREAMING_STOPPED = -1003,
	AIRSPY_ERROR_OTHER = -9999,
};

enum airspy_sample_type
{
	AIRSPY_SAMPLE_FLOAT32_IQ = 0,   /* 2 * 32bit float per sample */
	AIRSPY_SAMPLE_FLOAT32_REAL = 1, /* 1 * 32bit float per sample */
	AIRSPY_SAMPLE_INT16_IQ = 2,     /* 2 * 16bit int per sample */
	AIRSPY_SAMPLE_INT16_REAL = 3,   /* 1 * 16bit int per sample */
	AIRSPY_SAMPLE_UINT16_REAL = 4,  /* 1 * 16bit unsigned int per sample */
	AIRSPY_SAMPLE_RAW = 5,          /* Raw packed samples from the device */
	AIRSPY_SAMPLE_END = 6           /* Number of supported sample types */
};

typedef struct {
	struct airspy_device* device;
	void* ctx;
	void* samples;
	int sample_count;
	uint64_t dropped_samples;
	enum airspy_sample_type sample_type;
} airspy_transfer_t, airspy_transfer;

typedef struct {
	uint32_t major_version;
	uint32_t minor_version;
	uint32_t revision;
} airspy_lib_version_t;

typedef int (*airspy_sample_block_cb_fn)(airspy_transfer* transfer);

static int (*airspy_open)(struct airspy_device ** device);
static int (*airspy_close)(void * device);
static int (*airspy_set_samplerate)(void * device, uint32_t samplerate);
static int (*airspy_start_rx)(void * device, airspy_sample_block_cb_fn cb, void * ctx);
static int (*airspy_stop_rx)(void * device);
static int (*airspy_is_streaming)(void * device);
static int (*airspy_set_sample_type)(void * device, enum airspy_sample_type sample_type);
static int (*airspy_set_freq)(void * device, const uint32_t freq);
static int (*airspy_set_linearity_gain)(void * device, uint8_t gain);
static int (*airspy_set_sensitivity_gain)(void * device, uint8_t gain);
static int (*airspy_set_lna_gain)(void * device, uint8_t gain);
static int (*airspy_set_mixer_gain)(void * device, uint8_t gain);
static int (*airspy_set_vga_gain)(void * device, uint8_t gain);
static int (*airspy_set_lna_agc)(void * device, uint8_t gain);
static int (*airspy_set_mixer_agc)(void * device, uint8_t gain);
static void (*airspy_lib_version)(airspy_lib_version_t * lib_version);
static char * (*airspy_error_name)(enum airspy_error errcode);
// end of Airspy API defs

class SdrDeviceAirspy : public SdrDevice
{
public:
    SdrDeviceAirspy(void);
    virtual     ~SdrDeviceAirspy();

    // Virtual function implementations
    int         init(float samprate, const char * options);
    int         set_sample_rate(float new_rate);
    int         get_sample_rates(float * rates) const;
    float       get_sample_rate(void) const { return sample_rate; };
    float       get_dynamic_range(void) const { return 100.f; };
    int         set_freq(uint64_t freq);
    uint64_t    get_freq(void) const;
    int         get_freq_range(freq_range_t * range) const;
    int         set_freq_corr(float ppm);
    int         get_gain_stages(uint8_t * gains) const;
    uint16_t    get_gain_stages_bf(void) const;
    int         set_gain(uint8_t stage, uint8_t value);
    int         start(void);
    int         stop(void);
    uint32_t    get_num_bytes(void) const;
    uint32_t    get_num_samples(void) const;
    uint32_t    read_bytes(void * buffer, uint32_t bytes);
    uint32_t    read_samples(complex_t * buffer, uint32_t samples);

private:
    int         load_libairspy(void);
    void        free_memory(void);

    static int  airspy_rx_callback(airspy_transfer_t * transfer);

private:
    lib_handle_t            lib;
    struct airspy_device   *dev;

    uint64_t    total_samples;
    uint64_t    start_time;
    uint32_t    sample_rate;
    uint32_t    current_freq;   // Airspy max freq is 1.9 GHz (don't need 64 bit)

    ring_buffer_t  *sample_buffer;

    bool        initialized;
};

SdrDevice * sdr_device_create_airspy()
{
    return new SdrDeviceAirspy();
}

/* The callback function is a static method to allow access to private
 * members of the instance.
 */
int SdrDeviceAirspy::airspy_rx_callback(airspy_transfer_t * transfer)
{
    // we are in a static method, so we need to get the instance
    SdrDeviceAirspy *sdrdev = (SdrDeviceAirspy *) transfer->ctx;

    if (transfer->sample_type != AIRSPY_SAMPLE_FLOAT32_IQ)
    {
        fprintf(stderr, "Airspy is running with unsupported sample type: %d\n",
                transfer->sample_type);
        return -1;
    }

    sdrdev->total_samples += transfer->sample_count;
    ring_buffer_cplx_write(sdrdev->sample_buffer, (complex_t *)transfer->samples,
        transfer->sample_count);

    return 0;
}

SdrDeviceAirspy::SdrDeviceAirspy(void)
{
    lib = 0;
    dev = 0;
    total_samples = 0;
    start_time = 0;
    sample_rate = 0;
    current_freq = 0;
    sample_buffer = 0;
    initialized = false;
}

SdrDeviceAirspy::~SdrDeviceAirspy()
{
    if (!initialized)
        return;

    free_memory();

    if (airspy_is_streaming(dev))
        airspy_stop_rx(dev);

    airspy_close(dev);
    close_library(lib);
}

int SdrDeviceAirspy::load_libairspy()
{
    airspy_lib_version_t    lib_ver;

    fprintf(stderr, "Loading Airspy library... ");
    lib = load_library("airspy");
    if (lib == NULL)
    {
        fprintf(stderr, "Error loading library\n");
        return SDR_DEVICE_ELIB;
    }

    airspy_lib_version = (void (*)(airspy_lib_version_t *)) get_symbol(lib, "airspy_lib_version");
    if (airspy_lib_version == NULL)
        return SDR_DEVICE_ELIB;
    airspy_lib_version(&lib_ver);
    fprintf(stderr, "OK (version: %d.%d.%d)\n", lib_ver.major_version,
            lib_ver.minor_version, lib_ver.revision);
    if (lib_ver.major_version != AIRSPY_VER_MAJOR ||
        lib_ver.minor_version != AIRSPY_VER_MINOR)
        fprintf(stderr, "NOTE: Backend uses API version %d.%d\n",
                AIRSPY_VER_MAJOR, AIRSPY_VER_MINOR);

    airspy_open = (int (*)(struct airspy_device **)) get_symbol(lib, "airspy_open");
    if (airspy_open == NULL)
        return SDR_DEVICE_ELIB;

    airspy_close = (int (*)(void *)) get_symbol(lib, "airspy_close");
    if (airspy_close == NULL)
        return SDR_DEVICE_ELIB;

    airspy_set_samplerate = (int (*)(void *, uint32_t)) get_symbol(lib, "airspy_set_samplerate");
    if (airspy_set_samplerate == NULL)
        return SDR_DEVICE_ELIB;

    airspy_start_rx = (int (*)(void *, airspy_sample_block_cb_fn, void *))
                        get_symbol(lib, "airspy_start_rx");
    if (airspy_start_rx == NULL)
        return SDR_DEVICE_ELIB;

    airspy_stop_rx = (int (*)(void *)) get_symbol(lib, "airspy_stop_rx");
    if (airspy_stop_rx == NULL)
        return SDR_DEVICE_ELIB;

    airspy_is_streaming = (int (*)(void *)) get_symbol(lib, "airspy_is_streaming");
    if (airspy_is_streaming == NULL)
        return SDR_DEVICE_ELIB;

    airspy_set_sample_type = (int (*)(void *, enum airspy_sample_type)) get_symbol(lib, "airspy_set_sample_type");
    if (airspy_set_sample_type == NULL)
        return SDR_DEVICE_ELIB;

    airspy_set_freq = (int (*)(void *, uint32_t)) get_symbol(lib, "airspy_set_freq");
    if (airspy_set_freq == NULL)
        return SDR_DEVICE_ELIB;

    airspy_set_linearity_gain = (int (*)(void *, uint8_t)) get_symbol(lib, "airspy_set_linearity_gain");
    if (airspy_set_linearity_gain == NULL)
        return SDR_DEVICE_ELIB;

    airspy_set_sensitivity_gain = (int (*)(void *, uint8_t)) get_symbol(lib, "airspy_set_sensitivity_gain");
    if (airspy_set_sensitivity_gain == NULL)
        return SDR_DEVICE_ELIB;

    airspy_set_lna_gain = (int (*)(void *, uint8_t)) get_symbol(lib, "airspy_set_lna_gain");
    if (airspy_set_lna_gain == NULL)
        return SDR_DEVICE_ELIB;

    airspy_set_mixer_gain = (int (*)(void *, uint8_t)) get_symbol(lib, "airspy_set_mixer_gain");
    if (airspy_set_mixer_gain == NULL)
        return SDR_DEVICE_ELIB;

    airspy_set_vga_gain = (int (*)(void *, uint8_t)) get_symbol(lib, "airspy_set_vga_gain");
    if (airspy_set_vga_gain == NULL)
        return SDR_DEVICE_ELIB;

    airspy_set_lna_agc = (int (*)(void *, uint8_t)) get_symbol(lib, "airspy_set_lna_agc");
    if (airspy_set_lna_agc == NULL)
        return SDR_DEVICE_ELIB;

    airspy_set_mixer_agc = (int (*)(void *, uint8_t)) get_symbol(lib, "airspy_set_mixer_agc");
    if (airspy_set_mixer_agc == NULL)
        return SDR_DEVICE_ELIB;

    airspy_error_name = (char * (*)(enum airspy_error)) get_symbol(lib, "airspy_error_name");
    if (airspy_error_name == NULL)
        return SDR_DEVICE_ELIB;

    return SDR_DEVICE_OK;
}

int SdrDeviceAirspy::init(float samprate, const char * options)
{
    (void)      options;
    int         result;

    if (initialized)
        return SDR_DEVICE_OK;

    if (dev)
        return SDR_DEVICE_EBUSY;

    result = load_libairspy();
    if (result != SDR_DEVICE_OK)
        return result;

    sample_buffer = ring_buffer_cplx_create();
    if (!sample_buffer)
    {
        fputs("Failed to create sample buffer for Airspy.\n", stderr);
        return SDR_DEVICE_ERROR;
    }
    ring_buffer_init(sample_buffer, 1);

    result = airspy_open(&dev);
    if (result != AIRSPY_SUCCESS)
    {
        fprintf(stderr, "airspy_open() failed (%d): %s\n", result,
                airspy_error_name((enum airspy_error)result));
        return SDR_DEVICE_EOPEN;
    }

    // FIXME: check that sample rate is supported by device
    result = set_sample_rate(samprate);
    if (result != SDR_DEVICE_OK)
    {
        airspy_close(dev);
        return SDR_DEVICE_ESAMPRATE;
    }

    result = airspy_set_sample_type(dev, AIRSPY_SAMPLE_FLOAT32_IQ);
    if (result != AIRSPY_SUCCESS)
    {
        fprintf(stderr, "airspy_set_sample_type() failed (%d): %s\n", result,
                airspy_error_name((enum airspy_error)result));
        airspy_close(dev);
        return SDR_DEVICE_ERROR;
    }

    // set default gain
    result = airspy_set_linearity_gain(dev, 15);
    if (result != AIRSPY_SUCCESS)
    {
        fprintf(stderr, "airspy_set_linearity_gain() failed (%d): %s\n", result,
                airspy_error_name((enum airspy_error)result));
        airspy_close(dev);
        return SDR_DEVICE_ERROR;
    }

    initialized = true;

    return SDR_DEVICE_OK;
}

int SdrDeviceAirspy::set_sample_rate(float new_rate)
{
    uint32_t    idx;

    // FIXME: get table from libairspy
    if (new_rate == 10.e6 || new_rate == 6.0e6)
        idx = 0;
    else if (new_rate == 2.5e6 || new_rate == 3.0e6)
        idx = 1;
    else
        return SDR_DEVICE_EINVAL;

    int     result = airspy_set_samplerate(dev, idx);
    if (result != AIRSPY_SUCCESS)
    {
        fprintf(stderr, "airspy_set_samplerate(%"PRIu32") failed (%d): %s\n",
                sample_rate, result, airspy_error_name((enum airspy_error)result));
        return SDR_DEVICE_ERROR;
    }

    sample_rate = (uint32_t) new_rate;
    ring_buffer_cplx_resize(sample_buffer, 0.1f * new_rate);

    return SDR_DEVICE_OK;
}

int SdrDeviceAirspy::get_sample_rates(float * rates) const
{
    // FIXME: get table from libairspy
    if (rates != 0)
    {
        rates[0] = 2.5e6f;
        rates[1] = 3.0e6f;
        rates[2] = 6.0e6f;
        rates[3] = 10.e6f;
        return 4;
    }

    return 0;
}

int SdrDeviceAirspy::set_freq(uint64_t freq)
{
    current_freq = freq;

    int     result = airspy_set_freq(dev, current_freq);

    if (result != AIRSPY_SUCCESS)
    {
        fprintf(stderr, "airspy_set_freq(%"PRIu32") failed (%d): %s\n",
                current_freq, result,
                airspy_error_name((enum airspy_error)result));
        return SDR_DEVICE_ERANGE;
    }

    sdr_device_debug("SdrDeviceAirspy::set_freq(%"PRIu32")\n", current_freq);
    return SDR_DEVICE_OK;
}

uint64_t SdrDeviceAirspy::get_freq(void) const
{
    return current_freq;
}

int SdrDeviceAirspy::get_freq_range(freq_range_t * range) const
{
    range->min = 24e6;
    range->max = 1800e6;
    range->step = 1;
    return SDR_DEVICE_OK;
}

int SdrDeviceAirspy::set_freq_corr(float ppm)
{
    fputs("*** FIXME: set_freq_corr() not implemented for Airspy.\n", stderr);
    return SDR_DEVICE_OK;
}

int SdrDeviceAirspy::get_gain_stages(uint8_t * gains) const
{
    if (gains == 0)
        return 7;

    gains[0] = SDR_DEVICE_RX_LNA_GAIN;
    gains[1] = SDR_DEVICE_RX_MIX_GAIN;
    gains[2] = SDR_DEVICE_RX_VGA_GAIN;
    gains[3] = SDR_DEVICE_RX_LIN_GAIN;
    gains[4] = SDR_DEVICE_RX_SENS_GAIN;
    gains[5] = SDR_DEVICE_RX_RF_AGC;
    gains[6] = SDR_DEVICE_RX_IF_AGC;

    return 7;
}

uint16_t SdrDeviceAirspy::get_gain_stages_bf(void) const
{
    return (uint16_t)
        (1 << SDR_DEVICE_RX_LNA_GAIN) |
        (1 << SDR_DEVICE_RX_MIX_GAIN) |
        (1 << SDR_DEVICE_RX_VGA_GAIN) |
        (1 << SDR_DEVICE_RX_LIN_GAIN) |
        (1 << SDR_DEVICE_RX_SENS_GAIN) |
        (1 << SDR_DEVICE_RX_RF_AGC) |
        (1 << SDR_DEVICE_RX_IF_AGC);
}

int SdrDeviceAirspy::set_gain(uint8_t stage, uint8_t value)
{
    uint8_t     gain;
    int         retval = SDR_DEVICE_OK;

    if (value > 100)
        return SDR_DEVICE_ERANGE;

    switch (stage)
    {
    case SDR_DEVICE_RX_LNA_GAIN:
        gain = (value * 15) / 100;
        if (airspy_set_lna_gain(dev, gain) != AIRSPY_SUCCESS)
            retval = SDR_DEVICE_ERROR;
        break;
    case SDR_DEVICE_RX_MIX_GAIN:
        gain = (value * 15) / 100;
        if (airspy_set_mixer_gain(dev, gain) != AIRSPY_SUCCESS)
            retval = SDR_DEVICE_ERROR;
        break;
    case SDR_DEVICE_RX_VGA_GAIN:
        gain = (value * 15) / 100;
        if (airspy_set_vga_gain(dev, gain) != AIRSPY_SUCCESS)
            retval = SDR_DEVICE_ERROR;
        break;
    case SDR_DEVICE_RX_LIN_GAIN:
        gain = (value * 21) / 100;
        if (airspy_set_linearity_gain(dev, gain) != AIRSPY_SUCCESS)
            retval = SDR_DEVICE_ERROR;
        break;
    case SDR_DEVICE_RX_SENS_GAIN:
        gain = (value * 21) / 100;
        if (airspy_set_sensitivity_gain(dev, gain) != AIRSPY_SUCCESS)
            retval = SDR_DEVICE_ERROR;
        break;
    case SDR_DEVICE_RX_RF_AGC:
        if (airspy_set_lna_agc(dev, value ? 1 : 0) != AIRSPY_SUCCESS)
            retval = SDR_DEVICE_ERROR;
        break;
    case SDR_DEVICE_RX_IF_AGC:
        if (airspy_set_mixer_agc(dev, value ? 1 : 0) != AIRSPY_SUCCESS)
            retval = SDR_DEVICE_ERROR;
        break;
    default:
        retval = SDR_DEVICE_EINVAL;
        break;
    }

    sdr_device_debug("SdrDeviceAirspy::set_gain(stage:%u,val:%u)  result:%d\n",
                     stage, value, retval);

    return retval;
}

int SdrDeviceAirspy::start(void)
{
    int     result = airspy_start_rx(dev, airspy_rx_callback, this);
    if (result != AIRSPY_SUCCESS)
    {
        fprintf(stderr, "airspy_start_rx() failed (%d): %s\n", result,
                airspy_error_name((enum airspy_error)result));
        return SDR_DEVICE_ERROR;
    }

    total_samples = 0;
    start_time = time_ms();

    return SDR_DEVICE_OK;
}

int SdrDeviceAirspy::stop(void)
{
    int     result = airspy_stop_rx(dev);
    if (result != AIRSPY_SUCCESS)
    {
        fprintf(stderr, "airspy_stop_rx() failed (%d): %s\n", result,
                airspy_error_name((enum airspy_error)result));
        return SDR_DEVICE_ERROR;
    }

    uint64_t    dt = time_ms() - start_time;
    fprintf(stderr, "Airspy: Read %"PRIu64" samples in %"PRIu64" ms = %.4f Msps\n",
            total_samples, dt, 1.e-3f * (float)total_samples / (float)dt);

    return SDR_DEVICE_OK;
}

uint32_t SdrDeviceAirspy::get_num_bytes(void) const
{
    return 0;
}

uint32_t SdrDeviceAirspy::get_num_samples(void) const
{
    return ring_buffer_cplx_count(sample_buffer);
}

uint32_t SdrDeviceAirspy::read_bytes(void * buffer, uint32_t bytes)
{
    return 0;
}

#define SAMPLE_SCALE (1.0f / 32768.f)

uint32_t SdrDeviceAirspy::read_samples(complex_t * buffer, uint32_t samples)
{
    if (samples > ring_buffer_cplx_count(sample_buffer))
        return 0;

    ring_buffer_cplx_read(sample_buffer, buffer, samples);
    return samples;
}

void SdrDeviceAirspy::free_memory(void)
{
    if (sample_buffer)
        ring_buffer_cplx_delete(sample_buffer);
}
