#include "lpcsdr_hsadc.h"

#include "chip.h"
#include "stopwatch.h"

static uint32_t compute_mdec(uint32_t msel)
{
    /* from UM10503, 12.6.4.3 */
#define PLL0_MSEL_MAX (1<<15)
    switch (msel) {
    case 0: return 0;
    case 1: return 0x18003;
    case 2: return 0x10003;
    default:
        uint32_t x = 0x04000;
        for (uint32_t im = msel; im <= PLL0_MSEL_MAX; im++)
            x = (((x ^ (x>>1)) & 1) << 14) | ((x>>1) & 0xFFFF);
        return x;
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
            uint32_t x = 0x10;
            for (uint32_t ip = psel; ip <= PLL0_PSEL_MAX; ip++)
                x = (((x ^ (x>>2)) & 1) << 4) | ((x>>1) & 0x3F);
            return x;
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
            uint32_t x = 0x80;
            for (uint32_t in = nsel; in <= PLL0_NSEL_MAX; in++)
                x = (((x ^ x>>2 ^ x>>3 ^ x>>4) & 1) << 7) | (x>>1 & 0xFF);
            return x;
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

bool lpcsdr_hsadc_clock_start(const hsadc_clock_config_t *config)
{
    /* divisor sanity checks */
    if (config->n_divisor > 128)
        return false;

    if (config->p_divisor > 32)
        return false;

    if (config->idiv_divisor > 256)
        return false;

    uint32_t integer_m = config->m_divisor >> 15;
    bool fractional = (config->m_divisor & 0x3FFF) != 0;

    if (config->m_divisor == 0 || (!fractional && integer_m > 32768) || (fractional && integer_m > 128))
        return false;

    const CHIP_CGU_CLKIN_T clkin = CLKIN_CRYSTAL;

    /* derive fCCO, fADC */
    uint32_t fCCO;
    uint32_t fIn = 12000000;
    uint32_t fRef = fIn;
    if (config->n_divisor)
        fRef /= config->n_divisor;

    fCCO = (uint64_t)2 * config->m_divisor * fRef / 32768;
    if (fCCO < 275000000 || fCCO > 550000000)
        return false;

    uint32_t fPLL = fCCO;
    if (config->p_divisor)
        fPLL = fPLL / config->p_divisor / 2;

    uint32_t fADC = fPLL;
    if (config->idiv_divisor)
        fADC /= config->idiv_divisor;

    if (fADC > 80000000)
        return false;

    /* encode PLL0AUDIO register settings */
    uint32_t ctrl = PLL_CTRL_CLK_SEL(clkin);

    uint32_t mdiv;
    uint32_t fract;
    if (fractional) {
        mdiv = 0;
        fract = (config->m_divisor & 0x3FFFFF) << 0;
        ctrl |= PLL_CTRL_PLLFRACT_REQ;
    } else {
        /* integer mode */
        mdiv = (compute_mdec(integer_m) & 0x1FFFF) << 0;
        fract = 0;
        ctrl |= PLL_CTRL_SEL_EXT | PLL_CTRL_MOD_PD;
    }

    if (!config->n_divisor)
        ctrl |= PLL_CTRL_DIRECTI; /* bypass N-divider */
    if (!config->p_divisor)
        ctrl |= PLL_CTRL_DIRECTO; /* bypass P-divider */
    uint32_t npdiv = ((compute_pdec(config->p_divisor) & 0x7f)<<0) | ((compute_ndec(config->n_divisor) & 0x3FF)<<12);

    /* power down existing PLL */
    LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_CTRL |= PLL_CTRL_PD | PLL_CTRL_MOD_PD;
    StopWatch_DelayMs(10);

    /* reprogram PLL */
    LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_MDIV = mdiv;
    LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_NP_DIV = npdiv;
    LPC_CGU->PLL0AUDIO_FRAC = fract;
    StopWatch_DelayMs(10);
    LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_CTRL = ctrl;
    StopWatch_DelayMs(10);

    /* power up PLL */
    LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_CTRL &= ~PLL_CTRL_PD;
    StopWatch_DelayMs(10);

    /* wait for PLL lock */
    uint32_t start = StopWatch_Start();
    uint32_t timeout = StopWatch_MsToTicks(500);
    while (StopWatch_Elapsed(start) < timeout) {
        if (LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_STAT & PLL_STAT_LOCK)
            break;
    }

    /* enable PLL0AUDIO output */
    LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_CTRL |= PLL_CTRL_CLKEN;

    if (config->idiv_divisor) {
        /* PLL0AUDIO -> IDIV_E -> HSADC */
        Chip_Clock_SetDivider(CLK_IDIV_E, CLKIN_AUDIOPLL, config->idiv_divisor);
        Chip_Clock_SetBaseClock(CLK_BASE_ADCHS, CLKIN_IDIVE, true, false);
    } else {
        /* PLL0AUDIO -> HSADC, disable IDIV_E */
        Chip_Clock_SetDivider(CLK_IDIV_E, CLKINPUT_PD, 1);
        Chip_Clock_SetBaseClock(CLK_BASE_ADCHS, CLKIN_AUDIOPLL, true, false);
    }

    /* Enable ADC branch clock */
    Chip_Clock_EnableOpts(CLK_ADCHS, true, true, 1);
    return true;
}

void lpcsdr_hsadc_clock_stop(void)
{
    Chip_Clock_SetDivider(CLK_IDIV_E, CLKINPUT_PD, 1);
    LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_CTRL |= PLL_CTRL_PD | PLL_CTRL_MOD_PD;
}

void lpcsdr_hsadc_start()
{
    /* configure the ADC */
    Chip_HSADC_Init(LPC_ADCHS);

    /* basic config */
    Chip_HSADC_DisableInts(LPC_ADCHS, /* group */ 0, /* mask */ 0xFFFFFFF);
    Chip_HSADC_DisableInts(LPC_ADCHS, /* group */ 1, /* mask */ 0xFFFFFFF);
    Chip_HSADC_SetACDCBias(LPC_ADCHS, /* channel */ 0, /* dcInNeg */ HSADC_CHANNEL_DCBIAS, /* dcInPos */ HSADC_CHANNEL_DCBIAS);
    Chip_HSADC_SetPowerSpeed(LPC_ADCHS, /* two's complement */ true);
    Chip_HSADC_SetupFIFO(LPC_ADCHS, /* trip */ 8, /* packed */ true);
    Chip_HSADC_EnablePower(LPC_ADCHS);

    /* descriptor table with one entry that captures channel 0, then loops back to the top */
    Chip_HSADC_SetupDescEntry(LPC_ADCHS, /* table */ 0, /* descriptor */ 0, /* entry */ HSADC_DESC_CH(0) | HSADC_DESC_BRANCH_FIRST);
    Chip_HSADC_UpdateDescTable(LPC_ADCHS, /* table */ 0);
    Chip_HSADC_SetActiveDescriptor(LPC_ADCHS, /* table */ 0, /* descriptor */ 0);

    /* start collecting */
    Chip_HSADC_SWTrigger(LPC_ADCHS);
}

