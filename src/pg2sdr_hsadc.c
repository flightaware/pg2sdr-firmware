/*
 *  pg2sdr_hsadc.c - PG2 firmware, high-speed ADC setup
 *
 *  Copyright (c) 2026 FlightAware All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *  notice, this list of conditions and the following disclaimer in the
 *  documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "pg2sdr_hsadc.h"
#include "pg2sdr_uart.h"

#include "chip.h"
#include "stopwatch.h"

static bool config_dcinpos = true;
static bool config_dcinneg = true;
static bool config_twos = true;

static uint32_t compute_mdec(uint32_t msel)
{
    /* from UM10503, 12.6.4.3 */
#define PLL0_MSEL_MAX (1<<15)
    switch (msel) {
    case 0: return 0;
    case 1: return 0x18003;
    case 2: return 0x10003;
    default:
        {
            uint32_t x = 0x04000;
            for (uint32_t im = msel; im <= PLL0_MSEL_MAX; im++)
                x = (((x ^ (x>>1)) & 1) << 14) | ((x>>1) & 0xFFFF);
            return x;
        }
    }
}

static uint32_t compute_pdec(uint32_t psel)
{
    /* from UM10503, 12.6.4.4 */
#define PLL0_PSEL_MAX (1<<5)
    switch (psel) {
    case 0: return 0;
    case 1: return 0x62;
    case 2: return 0x42;
    default:
        {
            uint32_t x = 0x10;
            for (uint32_t ip = psel; ip <= PLL0_PSEL_MAX; ip++)
                x = (((x ^ (x>>2)) & 1) << 4) | ((x>>1) & 0x3F);
            return x;
        }
    }
}

static uint32_t compute_ndec(uint32_t nsel)
{
    /* from UM10503, 12.6.4.4 */
#define PLL0_NSEL_MAX (1<<8)
    switch (nsel) {
        case 0: return 0;
        case 1: return 0x302;
        case 2: return 0x202;
        default:
            {
                uint32_t x = 0x80;
                for (uint32_t in = nsel; in <= PLL0_NSEL_MAX; in++)
                    x = (((x ^ x>>2 ^ x>>3 ^ x>>4) & 1) << 7) | (x>>1 & 0xFF);
                return x;
            }
    }
}

/* PLL_STAT bits */
#define PLL_STAT_LOCK      _BIT(0)
#define PLL_STAT_FR        _BIT(1)

/* PLL_CTRL bits */
#define PLL_CTRL_PD           _BIT(0)
#define PLL_CTRL_BYPASS       _BIT(1)
#define PLL_CTRL_DIRECTI      _BIT(2)
#define PLL_CTRL_DIRECTO      _BIT(3)
#define PLL_CTRL_CLKEN        _BIT(4)
#define PLL_CTRL_FRM          _BIT(6)
#define PLL_CTRL_AUTOBLOCK    _BIT(11)
#define PLL_CTRL_PLLFRACT_REQ _BIT(12)
#define PLL_CTRL_SEL_EXT      _BIT(13)
#define PLL_CTRL_MOD_PD       _BIT(14)
#define PLL_CTRL_CLK_SEL(n)   (((n) & 0x1F) << 24)

#define PLL_MIN_FCCO (275000000)
#define PLL_MAX_FCCO (550000000)
#define CRYSTAL_FREQ (12000000)
#define HSADC_MAX_FREQ (80000000)

static uint32_t hsadc_frequency;  /* programmed frequency of PLL0AUDIO, 0 if inactive */
static bool hsadc_running;        /* true if HSADC block has been triggered & is running */

/* current HSADC clock configuration (if hsadc_frequency != 0) */
static uint32_t hsadc_n_divisor;
static uint32_t hsadc_m_divisor;
static uint32_t hsadc_p_divisor;
static uint32_t hsadc_idiv_divisor;

/* Configure the HSADC clock from XTAL with no PLL */
static bool set_hsadc_xtal(uint32_t idiv_divisor, uint32_t *fADCOut, CHIP_CGU_CLKIN_T *clockSrcOut)
{
    if (idiv_divisor) {
        /* XTAL -> IDIV_E -> HSADC */
        Chip_Clock_SetDivider(CLK_IDIV_E, CLKIN_CRYSTAL, idiv_divisor);
        *clockSrcOut = CLKIN_IDIVE;
        *fADCOut = CRYSTAL_FREQ / idiv_divisor;
    } else {
        /* XTAL -> HSADC, disable IDIV_E */
        Chip_Clock_SetDivider(CLK_IDIV_E, CLKINPUT_PD, 1);
        *clockSrcOut = CLKIN_CRYSTAL;
        *fADCOut = CRYSTAL_FREQ;
    }

    /* Power down PLL0AUDIO */
    LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_CTRL |= PLL_CTRL_PD | PLL_CTRL_MOD_PD;

    return true;
}

/* Configure the HSADC clock via PLL0AUDIO */
static bool set_hsadc_pll0audio(uint32_t n_divisor, uint32_t m_divisor, uint32_t p_divisor, uint32_t idiv_divisor,
                                uint32_t *fADCOut, CHIP_CGU_CLKIN_T *clockSrcOut)
{
    uint32_t integer_m = m_divisor >> 15;
    bool fractional = (m_divisor & 0x7FFF) != 0;

    if ((!fractional && integer_m > 32768) || (fractional && integer_m > 128))
        return false;

    /* derive fCCO, fADC */
    uint32_t fCCO;
    uint32_t fIn = CRYSTAL_FREQ;
    uint32_t fRef = fIn;
    if (n_divisor)
        fRef /= n_divisor;

    fCCO = (uint64_t)2 * m_divisor * fRef / 32768;
    if (fCCO < PLL_MIN_FCCO || fCCO > PLL_MAX_FCCO)
        return false;

    uint32_t fPLL = fCCO;
    if (p_divisor)
        fPLL = fPLL / p_divisor / 2;

    uint32_t fADC = fPLL;
    if (idiv_divisor)
        fADC /= idiv_divisor;

    if (fADC > HSADC_MAX_FREQ)
        return false;

    /* encode PLL0AUDIO register settings */
    uint32_t ctrl = PLL_CTRL_CLK_SEL(CLKIN_CRYSTAL) | PLL_CTRL_AUTOBLOCK;

    uint32_t mdiv;
    uint32_t fract;
    if (fractional) {
        mdiv = 0;
        fract = (m_divisor & 0x3FFFFF) << 0;
        ctrl |= PLL_CTRL_PLLFRACT_REQ;
    } else {
        /* integer mode */
        mdiv = (compute_mdec(integer_m) & 0x1FFFF) << 0;
        fract = 0;
        ctrl |= PLL_CTRL_SEL_EXT | PLL_CTRL_MOD_PD;
    }

    if (!n_divisor)
        ctrl |= PLL_CTRL_DIRECTI; /* bypass N-divider */
    if (!p_divisor)
        ctrl |= PLL_CTRL_DIRECTO; /* bypass P-divider */
    uint32_t npdiv = ((compute_pdec(p_divisor) & 0x7f)<<0) | ((compute_ndec(n_divisor) & 0x3FF)<<12);

    /* power down existing PLL */
    LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_CTRL |= PLL_CTRL_PD | PLL_CTRL_MOD_PD;

    /* reprogram PLL */
    LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_MDIV = mdiv;
    LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_NP_DIV = npdiv;
    LPC_CGU->PLL0AUDIO_FRAC = fract;
    LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_CTRL = ctrl;

    /* power up PLL */
    LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_CTRL &= ~PLL_CTRL_PD;

    /* wait up to 100ms for PLL lock */
    uint32_t start = StopWatch_Start();
    uint32_t timeout = StopWatch_MsToTicks(100);
    while (StopWatch_Elapsed(start) < timeout) {
        if (LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_STAT & PLL_STAT_LOCK)
            break;
    }
    /* PLL lock doesn't seem very reliable, so don't treat a lock failure as an error here */
    if (!(LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_STAT & PLL_STAT_LOCK)) {
        debug_printf("ADC PLL did not lock at %u Hz within 100ms\r\n", fPLL);
    }

    /* enable PLL0AUDIO output */
    LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_CTRL |= PLL_CTRL_CLKEN;

    if (idiv_divisor) {
        /* PLL0AUDIO -> IDIV_E -> HSADC */
        Chip_Clock_SetDivider(CLK_IDIV_E, CLKIN_AUDIOPLL, idiv_divisor);
        *clockSrcOut = CLKIN_IDIVE;
    } else {
        /* PLL0AUDIO -> HSADC, disable IDIV_E */
        Chip_Clock_SetDivider(CLK_IDIV_E, CLKINPUT_PD, 1);
        *clockSrcOut = CLKIN_AUDIOPLL;
    }

    *fADCOut = fADC;
    return true;
}

bool pg2sdr_hsadc_clock_start(uint32_t n_divisor,     /* PLL0AUDIO pre-divisor (0 = bypass pre-divider) */
                              uint32_t m_divisor,     /* PLL0AUDIO feedback divisor, fixed point, 15 bit fractional part (0 = bypass PLL0AUDIO) */
                              uint32_t p_divisor,     /* PLL0AUDIO post-divisor (0 = bypass post-divider) */
                              uint32_t idiv_divisor)  /* IDIV_E divisor (0 = bypass IDIV_E) */
{
    /* divisor sanity checks */
    if (n_divisor > 256)
        return false;

    if (p_divisor > 32)
        return false;

    if (idiv_divisor > 256)
        return false;

    if (hsadc_frequency != 0 &&
        hsadc_n_divisor == n_divisor &&
        hsadc_m_divisor == m_divisor &&
        hsadc_p_divisor == p_divisor &&
        hsadc_idiv_divisor == idiv_divisor) {
        /* already configured like this */
        return true;
    }

    /* Configure HSADC clock source */
    uint32_t fADC;
    CHIP_CGU_CLKIN_T adcClockSrc;
    if (!m_divisor) {
        if (!set_hsadc_xtal(idiv_divisor, &fADC, &adcClockSrc))
            return false;
    } else {
        if (!set_hsadc_pll0audio(n_divisor, m_divisor, p_divisor, idiv_divisor, &fADC, &adcClockSrc))
            return false;
    }

    /* Enable HSADC branch clock */
    Chip_Clock_SetBaseClock(CLK_BASE_ADCHS, adcClockSrc, true, false);
#ifdef HW_HAS_CLKOUT
    Chip_Clock_SetBaseClock(CLK_BASE_OUT, adcClockSrc, true, false);
#endif
    Chip_Clock_EnableOpts(CLK_ADCHS, true, true, 1);

    hsadc_frequency = fADC;
    hsadc_n_divisor = n_divisor;
    hsadc_m_divisor = m_divisor;
    hsadc_p_divisor = p_divisor;
    hsadc_idiv_divisor = idiv_divisor;

    return true;
}

void pg2sdr_hsadc_clock_stop(void)
{
    if (!hsadc_frequency) {
        /* ADC clock not configured yet, bail out  */
        return;
    }

    if (hsadc_running) {
        /* best to stop the ADC first before messing with the branch clock */
        pg2sdr_hsadc_conversion_stop();
    }

    /* Disable ADC branch clock */
    Chip_Clock_Disable(CLK_ADCHS);
    /* Power down PLL/divider */
    Chip_Clock_SetDivider(CLK_IDIV_E, CLKINPUT_PD, 1);
    LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_CTRL |= PLL_CTRL_PD | PLL_CTRL_MOD_PD;
    /* Power down base clocks */
    Chip_Clock_SetBaseClock(CLK_BASE_ADCHS, CLKINPUT_PD, true, true);
    Chip_Clock_SetBaseClock(CLK_BASE_OUT, CLKINPUT_PD, true, true);
    hsadc_frequency = 0;
}

void pg2sdr_hsadc_init()
{
#ifdef HW_HAS_CLKOUT
    /* Enable CLK0/CLK2 for ADC clock measurement */
    Chip_SCU_ClockPinMuxSet(0, SCU_MODE_FUNC1 | SCU_MODE_INACT);
    Chip_SCU_ClockPinMuxSet(2, SCU_MODE_FUNC1 | SCU_MODE_INACT);
    Chip_Clock_EnableBaseClock(CLK_BASE_OUT);
#endif

    /* Enable register clock, reset ADC */
    Chip_Clock_EnableOpts(CLK_MX_ADCHS, true, true, 1);
    Chip_RGU_TriggerReset(RGU_ADCHS_RST);

    /* Disable IDIVE until we're first asked to configure the clock */
    Chip_Clock_SetDivider(CLK_IDIV_E, CLKINPUT_PD, 1);
}

bool pg2sdr_hsadc_conversion_start()
{
    if (!hsadc_frequency) {
        /* ADC clock not configured yet, bail out  */
        return false;
    }

    if (hsadc_running) {
        /* already active, do nothing */
        return true;
    }

    /* wait for any pending reset to complete */
    while (Chip_RGU_InReset(RGU_ADCHS_RST))
        __NOP();

    /* basic config */
    LPC_ADCHS->INTS[0].CLR_EN = 0xFFFFFFFF; // interrupt 0, disable all interrupts
    LPC_ADCHS->INTS[1].CLR_EN = 0xFFFFFFFF; // interrupt 1, disable all interrupts
    LPC_ADCHS->FIFO_CFG =
            _BIT(0) |   // PACKED_READ, pack two samples per FIFO word
            (8 << 1);   // FIFO_LEVEL = 8, raise DMA request when the FIFO has >= 8 words
    LPC_ADCHS->CONFIG =
            (1 << 0) |  // TRIGGER_MASK = 1, software trigger only
            _BIT(5)  |  // CHANNEL_ID_EN, store channel IDs (always zero)
            (144 << 6); // RECOVERY_TIME = 144 fADC cycles

    // see UM10503 47.6.11 and 47.6.12 for ADC/speed selection rules
    uint32_t crs, adc_speed;
    if (hsadc_frequency > 65000000) {
        crs = 4;
        adc_speed = 0x00EEEEEE;
    } else if (hsadc_frequency > 50000000) {
        crs = 3;
        adc_speed = 0x00FFFFFF;
    } else if (hsadc_frequency > 30000000) {
        crs = 2;
        adc_speed = 0;
    } else if (hsadc_frequency > 20000000) {
        crs = 1;
        adc_speed = 0;
    } else {
        crs = 0;
        adc_speed = 0;
    }

    uint32_t dcinpos = (config_dcinpos ? 0x3F : 0);
    uint32_t dcinneg = (config_dcinneg ? 0x3F : 0);
    uint32_t twos = (config_twos ? 1 : 0);

    LPC_ADCHS->POWER_CONTROL =
            (crs << 0)      |    // CRS
            (dcinneg << 4)  |    // DCINNEG=0/1, configure DC bias, negative side, all channels
            (dcinpos << 10) |    // DCINPOS=0/1, configure DC bias, positive side, all channels
            (twos << 16)    |    // TWOS=0/1, configure output format to offset binary or two's complement
            _BIT(17)        |    // POWER_SWITCH=1, ADC active
            _BIT(18);            // BGAP_SWITCH=1, band gap reference active
    LPC_ADCHS->ADC_SPEED = adc_speed;

    // Populate descriptor tables, with extra paranoia
    for (unsigned i = 0; i < 6; ++i) {
            LPC_ADCHS->DESCRIPTOR[0][i] = // Subsequent samples, convert on every fADC cycle
                    (0 << 0) |            // CHANNEL_NR=0, convert channel 0
                    (0x01 << 6) |         // BRANCH=1, branch to first descriptor of this table (table 0)
                    _BIT(24);             // RESET_TIMER=1, reset timer
    }
    LPC_ADCHS->DESCRIPTOR[0][7] = // First sample, wait for RECOVERY_TIME before first conversion
            (0 << 0) |            // CHANNEL_NR=0, convert channel 0
            (0x01 << 6) |         // BRANCH=1, branch to first descriptor of this table
            (144 << 8) |          // MATCH_VALUE=144, execute after 144 fADC cycles
            _BIT(24);             // RESET_TIMER=1, reset timer

    for (unsigned i = 0; i < 7; ++i) {
        LPC_ADCHS->DESCRIPTOR[1][i] = // Subsequent samples, convert on every fADC cycle
                (0 << 0) |            // CHANNEL_NR=0, convert channel 0
                (0x02 << 6) |         // BRANCH=2, branch to first descriptor of other table (table 0)
                _BIT(24);             // RESET_TIMER=1, reset timer
    }

    LPC_ADCHS->DESCRIPTOR[0][0] |= _BIT(31); // Load table 0 into shadow table
    LPC_ADCHS->DESCRIPTOR[1][0] |= _BIT(31); // Load table 1 into shadow table

    LPC_ADCHS->DSCR_STS =
            (0 << 0) |      // active descriptor table = 0
            (7 << 1);       // active descriptor index = 7

    LPC_ADCHS->POWER_DOWN = 0; // clear power-down bit

    /* wait 110us for powerup */
    StopWatch_DelayUs(110);

    LPC_ADCHS->FLUSH = 1;      // clear FIFO, just in case

    /* clear stale status bits */
    LPC_ADCHS->INTS[0].CLR_STAT = 0xFFFFFFFF;
    LPC_ADCHS->INTS[1].CLR_STAT = 0xFFFFFFFF;

    /* software trigger, go */
    LPC_ADCHS->TRIGGER = 1;

    hsadc_running = true;
    return true;
}

void pg2sdr_hsadc_conversion_stop()
{
    if (!hsadc_running) {
        /* not running, nothing to do */
        return;
    }

    /* trigger reset, don't wait for completion */
    Chip_RGU_TriggerReset(RGU_ADCHS_RST);
    hsadc_running = false;
}

void pg2sdr_hsadc_status(ep0_in_board_status_t *status)
{
    status->hsadc_frequency = hsadc_frequency;
    if (hsadc_frequency) {
        if (!(LPC_ADCHS->POWER_DOWN & 1)) {
            status->flags |= STATUS_HSADC_RUN;
        }

        if (!(LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_CTRL & PLL_CTRL_PD)) {
            status->flags |= STATUS_PLL0AUDIO_RUN;
        }

        status->pll_stat = LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_STAT;
        status->pll_ctrl = LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_CTRL;
        status->pll_mdiv = LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_MDIV;
        status->pll_np_div = LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_NP_DIV;
        status->pll_frac = LPC_CGU->PLL0AUDIO_FRAC;
        status->idiv_e_ctrl = LPC_CGU->IDIV_CTRL[CLK_IDIV_E];

        status->adchs_fifo_cfg = LPC_ADCHS->FIFO_CFG;
        status->adchs_config = LPC_ADCHS->CONFIG;
        status->adchs_adc_speed = LPC_ADCHS->ADC_SPEED;
        status->adchs_power_control = LPC_ADCHS->POWER_CONTROL;
        status->adchs_int0_status = LPC_ADCHS->INTS[0].STATUS;
        status->adchs_fifo_sts = LPC_ADCHS->FIFO_STS;
        status->adchs_dscr_sts = LPC_ADCHS->DSCR_STS;
    }
}

void pg2sdr_hsadc_set_config(bool dcinpos, bool dcinneg, bool twos)
{
    config_dcinpos = dcinpos;
    config_dcinneg = dcinneg;
    config_twos = twos;

    if (hsadc_frequency) {
        uint32_t dcinpos = (config_dcinpos ? 0x3F : 0);
        uint32_t dcinneg = (config_dcinneg ? 0x3F : 0);
        uint32_t twos = (config_twos ? 1 : 0);

        uint32_t control = LPC_ADCHS->POWER_CONTROL;
        control = (control & ~(0x3F << 4)) | (dcinneg << 4);
        control = (control & ~(0x3F << 10)) | (dcinpos << 10);
        control = (control & ~(1 << 16)) | (twos << 16);
        LPC_ADCHS->POWER_CONTROL = control;
    }
}

uint32_t pg2sdr_hsadc_get_sampling_rate()
{
    return hsadc_frequency;
}
