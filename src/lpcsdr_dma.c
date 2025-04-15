#include "lpcsdr_common.h"
#include "lpcsdr_dma.h"
#include "lpcsdr_usb.h"
#include "lpcsdr_gpio.h"
#include "lpcsdr_protocol.h"

#include "chip.h"

/*
 * The chip library for GPDMA doesn't fit so well with what we're doing here,
 * so we do everything ourselves instead.
 */

/* Pointers to DMA buffers that will be filled by the DMA controller with ADC data */
static uint32_t *hsadc_dma_buffer[HSADC_NUM_BUFFERS];

/* A DMA LLI transfer descriptor for each buffer */
static dma_lli_t hsadc_dma_lli[HSADC_NUM_BUFFERS];

dma_lli_t *hsadc_current_lli; /* Next LLI awaiting completion */
uint32_t hsadc_next_sequence; /* Buffer sequence number included in transferred buffers (so the host can maintain a correct sample timestamp) */

static uint32_t pending_dma_status; /* flags awaiting notification to the packing layer */

void lpcsdr_dma_init(void)
{
    static_assert( HSADC_BUFFER_SIZE % 4 == 0 ); /* DMA buffer size must be a whole number of 32-bit words */
    static_assert( HSADC_NUM_BUFFERS % 2 == 0 ); /* Need an even number of DMA buffers so we can alternate allocation between SRAM banks */
    static_assert( HSADC_NUM_BUFFERS * HSADC_BUFFER_SIZE <= 0x10000 );  /* All buffers must fit into available SRAM */
    static_assert( HSADC_BUFFER_SIZE / 4 * 3 + 20 <= DTD_BUFFER_SIZE ); /* Each DMA buffer, after packing, must fit into the available USB buffer space */

    /* enable DMA controller register clock */
    Chip_Clock_EnableOpts(CLK_MX_DMA, true, true, 1);

    /* set DMAMUXPER8 = 0x3: peripheral 8 = ADCHS read; rest of DMAMUX is don't-care */
    LPC_CREG->DMAMUX = (3 << 16);

    /* enable DMA controller */
    LPC_GPDMA->CONFIG = GPDMA_DMACConfig_E;  // controller enabled, both AHB masters in little-endian mode
    while (!(LPC_GPDMA->CONFIG & GPDMA_DMACConfig_E))
        __NOP(); // wait until controller is ready

    /* clear all channels */
    for (unsigned i = 0; i < 8; ++i)
        LPC_GPDMA->CH[i].CONFIG = 0;

    /* reset all interrupts */
    LPC_GPDMA->INTTCCLEAR = 0xFF;
    LPC_GPDMA->INTERRCLR = 0xFF;

    /* enable NVIC DMA interrupt */
    NVIC_EnableIRQ(DMA_IRQn);
}

void lpcsdr_dma_hsadc_start(void) {
    lpcsdr_dma_hsadc_stop();

    /* Create a loop of transfer descriptor LLIs, pointing to buffers
     * that alternate between AHB_SRAM_BANK_0 and AHB_SRAM_BANK_1.
     * The DMA controller will follow the loop and continuously fill
     * buffers, producing an interrupt as each individual buffer is
     * completed, but never terminating (at least until hsadc_stop
     * is called).
     *
     * Ideally, the main loop will be processing some buffer X while
     * the DMA controller is filling the next buffer X+1. Because X and
     * X+1 are in different SRAM banks, the CPU and the DMA controller
     * are not competing for access to the same bank of memory.
     */
    for (unsigned i = 0; i < HSADC_NUM_BUFFERS; ++i) {
        uint32_t buffer;
        if (i % 2 == 0)
            buffer = AHB_SRAM_BANK_0 + (i/2) * HSADC_BUFFER_SIZE;
        else
            buffer = AHB_SRAM_BANK_1 + (i/2) * HSADC_BUFFER_SIZE;

        hsadc_dma_buffer[i] = (uint32_t *)buffer;
        hsadc_dma_lli[i].srcaddr = (uint32_t) &LPC_ADCHS->FIFO_OUTPUT[0];      // source = HSADC FIFO read
        hsadc_dma_lli[i].destaddr = buffer; // destination address = start of buffer
        hsadc_dma_lli[i].lli = (uint32_t) &hsadc_dma_lli[(i+1) % HSADC_NUM_BUFFERS]; /* LM=0, load using AHB Master 0 */
        hsadc_dma_lli[i].control =
                GPDMA_DMACCxControl_TransferSize(HSADC_BUFFER_SIZE/4) |   /* size in number of transfers, transfer size = 1 word */
                GPDMA_DMACCxControl_SBSize(GPDMA_BSIZE_8) |               /* source (ADC) burst = 8 words (half the ADC FIFO size) */
                GPDMA_DMACCxControl_DBSize(GPDMA_BSIZE_8) |               /* dest (memory) burst = 8 words */
                GPDMA_DMACCxControl_SWidth(GPDMA_WIDTH_WORD) |
                GPDMA_DMACCxControl_DWidth(GPDMA_WIDTH_WORD) |
                GPDMA_DMACCxControl_SrcTransUseAHBMaster1 |               /* source (ADC): use master 1 (required for peripherals) */
                /* dest (memory): use master 0 */
                /* SI=0, no source address increment */
                GPDMA_DMACCxControl_DI |  // DI=1, increment destination address
                /* PROT1/2/3: unused */
                GPDMA_DMACCxControl_I;    // enable terminal count interrupt
        hsadc_dma_lli[i].status = 0;
        hsadc_dma_lli[i].sequence = 0;
    }

    hsadc_current_lli = &hsadc_dma_lli[0];
    hsadc_next_sequence = 1;
    pending_dma_status = 0;

    /* load initial channel 0 LLI */
    LPC_GPDMA->CH[0].SRCADDR = hsadc_dma_lli[0].srcaddr;
    LPC_GPDMA->CH[0].DESTADDR = hsadc_dma_lli[0].destaddr;
    LPC_GPDMA->CH[0].LLI = hsadc_dma_lli[0].lli;
    LPC_GPDMA->CH[0].CONTROL = hsadc_dma_lli[0].control;

    memory_barrier();

    /* configure and enable channel 0 */
    LPC_GPDMA->CH[0].CONFIG =
        GPDMA_DMACCxConfig_E |                 // channel enabled
        GPDMA_DMACCxConfig_SrcPeripheral(8) |  // source peripheral = ADCHS read
        GPDMA_DMACCxConfig_DestPeripheral(0) | // dest peripheral = don't care (memory)
        GPDMA_DMACCxConfig_TransferType(GPDMA_TRANSFERTYPE_P2M_CONTROLLER_DMA) | // transfer peripheral to memory, DMA controller flow control
        GPDMA_DMACCxConfig_IE |                // enable error interrupts
        GPDMA_DMACCxConfig_ITC;                // enable terminal count interrupts
}

void lpcsdr_dma_hsadc_stop(void)
{
    /* halt channel 0 if active */
    LPC_GPDMA->CH[0].CONFIG = 0; /* disable channel, mask interrupts */
    while (LPC_GPDMA->CH[0].CONFIG & GPDMA_DMACCxConfig_A)
        __NOP(); /* wait until channel is not active */
    /* reset any outstanding channel 0 interrupts */
    LPC_GPDMA->INTTCCLEAR = 0x01;
    LPC_GPDMA->INTERRCLR = 0x01;
    /* we're no longer transferring data, LED should be off */
    lpcsdr_led_set(1, C_OFF);
}

static void hsadc_dma_err(void)
{
    /* handle a DMA error interrupt on DMA channel 0.. somehow */
    pending_dma_status |= BLOCK_STATUS_DMA_ERROR;
}

static void hsadc_dma_tc(void)
{
    /* Handle a DMA terminal-count interrupt on channel 0.
     * This interrupt is raised when the DMA controller reaches
     * the end of one LLI (with the I flag set) and moves on to the next.
     *
     * We need to handle this before the next LLI completes,
     * but we have a "lot" of time (0.3ms at 20MHz sampling rate) before
     * that happens so even though there's no reliable way to check for
     * multiple LLI completion, we're _probably_ okay here?
     *
     * For some extra paranoia, we advance forward until our
     * idea of what the "current" LLI is, matches what the hardware thinks.
     */

    static uint32_t recent_drops; /* used to track recently dropped data, so we can update LED state */

    /* retrieve and clear latest ADC status, we will attribute this to each completed LLI */
    uint32_t adc_status = LPC_ADCHS->INTS[0].STATUS;
    LPC_ADCHS->INTS[0].CLR_STAT = adc_status;

    if (adc_status & _BIT(2))
        pending_dma_status |= BLOCK_STATUS_ADC_OVERRUN;
    if (adc_status & _BIT(5))
        pending_dma_status |= BLOCK_STATUS_ADC_OVF;
    if (adc_status & _BIT(6))
        pending_dma_status |= BLOCK_STATUS_ADC_UNF;

    /* Walk forward through the LLI loop, handling items, until our idea of
     * the currently-in-use LLI matches what the DMA hardware reports
     */
    while (hsadc_current_lli->lli != LPC_GPDMA->CH[0].LLI) {
        dma_lli_t *completed = hsadc_current_lli;
        hsadc_current_lli = (dma_lli_t*) completed->lli;

        completed->sequence = hsadc_next_sequence++;
        if (!(completed->status & LLI_STATUS_COPYING)) {
            /* Happy path, this is a freshly completed LLI ready to hand off to the main loop */
            if (completed->status & LLI_STATUS_DROPPED) {
                /* Main loop dropped the previous copy of this buffer, count it in recent-drops */
                ++recent_drops;
            }

            completed->status = LLI_STATUS_COPYING; /* not clobbered, not dropped */
            if (lpcsdr_dma_hsadc_buffer_ready(completed, pending_dma_status)) {
                /* main loop will copy data out, and then clear the COPYING bit.
                 * we have successfully notified the main loop of all pending
                 * dma status bits.
                 */
                pending_dma_status = 0;
            } else {
                /* main loop couldn't accept the buffer, drop it, maintain
                 * dma status bits for next attempt
                 */
                completed->status = 0;
                pending_dma_status |= BLOCK_STATUS_PACKING_OVERRUN;
                ++recent_drops;
            }
        } else {
            /* If the just-completed LLI was already marked as COPYING, then that means
             * the main loop is still busy with the old copy. Don't resubmit a new copy,
             * let the main loop eventually finish with the old copy.
             */
            ++recent_drops;
        }

        /* At this point, the DMA controller has started to fill hsadc_current_lli; mark that
         * buffer as clobbered, so the main loop can notice if it's still working on that
         * buffer from a previous fill.
         */
        if ((hsadc_current_lli->status & (LLI_STATUS_COPYING | LLI_STATUS_CLOBBERED)) == LLI_STATUS_COPYING)
            ++recent_drops; /* we clobbered a buffer that was previously accepted but which hadn't completed yet */
        hsadc_current_lli->status |= LLI_STATUS_CLOBBERED;

        /* periodically update LED 1, every 256 buffers (~10Hz update rate at 20MHz sampling rate) */
        if (!(hsadc_next_sequence & 255)) {
            if (!recent_drops) {
                /* no recent data dropped */
                lpcsdr_led_set(1, C_GREEN);
            } else if (recent_drops > 128) {
                /* >50% recent data dropped */
                lpcsdr_led_set(1, C_RED);
            } else {
                /* >0 but <50% dropped */
                lpcsdr_led_set(1, C_YELLOW);
            }
            recent_drops = 0;
        }
    }
}

void DMA_IRQHandler(void)
{
    /* DMA ISR, dispatch interrupts to channel handlers (i.e. only channel 0 currently) */

    uint32_t inttcstat = LPC_GPDMA->INTTCSTAT;
    LPC_GPDMA->INTTCCLEAR = inttcstat;
    if (inttcstat & 1)
        hsadc_dma_tc();

    uint32_t interrstat = LPC_GPDMA->INTERRSTAT;
    LPC_GPDMA->INTERRCLR = interrstat;
    if (interrstat & 1)
        hsadc_dma_err();
}
