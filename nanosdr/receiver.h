#pragma once

#include <stdint.h>

#include "common/datatypes.h"
#include "common/sdr_data.h"
#include "nanodsp/agc.h"
#include "nanodsp/amdemod.h"
#include "nanodsp/fastfir.h"
#include "nanodsp/filter/decimator.h"
#include "nanodsp/fract_resampler.h"
#include "nanodsp/nfm_demod.h"
#include "nanodsp/smeter.h"
#include "nanodsp/ssbdemod.h"
#include "nanodsp/translate.h"


class Receiver
{
public:
    Receiver();
    virtual ~Receiver();

    void init(real_t in_rate, real_t out_rate, real_t dyn_range,
              uint32_t frame_length);
    void set_tuning_offset(real_t offset);
    void set_filter(real_t low_cut, real_t high_cut);
    void set_cw_offset(real_t offset);
    void set_agc(int threshold, int slope, int decay);
    void set_demod(sdr_demod_t new_demod);
    void set_sql(real_t level)
    {
        sql_level = level;
    }

    int process(int input_length, complex_t * input, real_t * output);

    real_t  get_signal_strength(void) const;

private:
    void free_memory(void);

private:
    FastFIR     filter;
    Decimator   decim;
    SMeter      meter;
    CAgc        agc;
    NfmDemod    nfm;
    AmDemod     am;
    SsbDemod    ssb;
    Translate   vfo;
    Translate   bfo;            // used to generate CW tone
    FractResampler  audio_resampler;

    real_t      sql_level;
    real_t      input_rate;
    real_t      quad_rate;
    real_t      output_rate;
    real_t      audio_rr;

    unsigned int    quad_decim;

    uint8_t     demod;
    uint32_t    buflen;
    complex_t  *cplx_buf0;
    complex_t  *cplx_buf1;
    complex_t  *cplx_buf2;
    real_t     *real_buf1;
};

