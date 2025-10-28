#include "pg2sdr_ipc.h"

#include <string.h>
#include "chip.h"

#include "pg2sdr_common.h"
#include "pg2sdr_m4clock.h"

void pg2sdr_ipc_receive(ipc_mailbox_t *mailbox, ipc_message_handler_t handler)
{
    // nb: no mutual exclusion on the reader side, it's assumed there
    // is only a single reader
    uint32_t head = mailbox->head;
    while (head != mailbox->tail) {
        ipc_message_t message = mailbox->queue[head];
        mailbox->head = head = (head + 1) % IPC_MAILBOX_SIZE;
        handler(&message);
    }
}

bool pg2sdr_ipc_pending(ipc_mailbox_t *mailbox)
{
    return (mailbox->head != mailbox->tail);
}

// It would be nice to have the M4 use LDREX/STREX and avoid disabling
// interrupts here, but doing that correctly is surprisingly tricky (without heap
// allocations), so for now we just entirely disable interrupts on both M4 and M0 for
// the duration of this code. Caller must disable interrupts!
static bool ipc_send(ipc_mailbox_t *mailbox, uint32_t message, uint32_t value0, uint32_t value1, uint32_t value2)
{
    uint32_t tail = mailbox->tail;
    uint32_t next = (tail + 1) % IPC_MAILBOX_SIZE;
    if (next == mailbox->head) {
        return false; // Queue is full
    }

    mailbox->queue[tail].message = message;
    mailbox->queue[tail].values[0] = value0;
    mailbox->queue[tail].values[1] = value1;
    mailbox->queue[tail].values[2] = value2;
    memory_barrier();
    mailbox->tail = next;
    return true;
}

#if defined(CORE_M4)

volatile bool m4_wakeup_requested;

void pg2sdr_ipc_init()
{
    memset(m4_to_m4_mailbox, 0, sizeof(ipc_mailbox_t));
    memset(m4_to_m0_mailbox, 0, sizeof(ipc_mailbox_t));
    memset(m0_to_m4_mailbox, 0, sizeof(ipc_mailbox_t));
    memset(m0_to_m0_mailbox, 0, sizeof(ipc_mailbox_t));
    NVIC_EnableIRQ(M0APP_IRQn);
}

void M0APP_IRQHandler(void)
{
    m4_wakeup_requested = true;
    Chip_CREG_ClearM0AppEvent();
    ++pg2sdr_interrupts.m0app;
}

bool pg2sdr_ipc_send_m4(m4_ipc_message_type_t message, uint32_t value0, uint32_t value1, uint32_t value2)
{
    if (!IS_M4_MESSAGE(message))
        return false;
    bool result;
    WITH_DISABLED_INTERRUPTS {
        result = ipc_send(m4_to_m4_mailbox, message, value0, value1, value2);
        if (result)
            m4_wakeup_requested = true;
    }
    return result;
}

bool pg2sdr_ipc_send_m0(m0_ipc_message_type_t message, uint32_t value0, uint32_t value1, uint32_t value2)
{
    if (!IS_M0_MESSAGE(message))
        return false;
    bool result;
    WITH_DISABLED_INTERRUPTS {
        result = ipc_send(m4_to_m0_mailbox, message, value0, value1, value2);
        if (result) {
            shared_memory_barrier(); // Ensure that the queue changes have really hit memory before signaling the other core
            __SEV();
        }
    }
    return result;
}

/* If there's no IPC traffic for a while and the main loop is asleep in WFI, the
 * watchdog warning interrupt will fire. Just trigger a wakeup so the main loop
 * can feed the watchdog.
 */
void WDT_IRQHandler(void)
{
    m4_wakeup_requested = true;
    Chip_WWDT_ClearStatusFlag(LPC_WWDT, WWDT_WDMOD_WDINT);
    ++pg2sdr_interrupts.wwdt;
}

void pg2sdr_ipc_handle_messages_forever(ipc_message_handler_t handler)
{
    /* Initialize watchdog, require feeding every 5 seconds */
    Chip_Clock_Enable(CLK_MX_WWDT);
    Chip_WWDT_Init(LPC_WWDT);
    Chip_WWDT_SetTimeOut(LPC_WWDT, WDT_OSC * 5 / 4); /* 5 seconds; watchdog counts down at WDT_OSC/4 */
    Chip_WWDT_SetWarning(LPC_WWDT, 1023);            /* generate interrupt ~0.3ms (4096 cycles @ WDT_OSC) before watchdog timeout */
    Chip_WWDT_SetOption(LPC_WWDT, WWDT_WDMOD_WDEN | WWDT_WDMOD_WDRESET); /* enable watchdog, reset chip on watchdog timeout */
    NVIC_EnableIRQ(WWDT_IRQn);

    while (true) {
        pg2sdr_ipc_receive(m4_to_m4_mailbox, handler);
        pg2sdr_ipc_receive(m0_to_m4_mailbox, handler);
        __disable_irq();
        if (!m4_wakeup_requested) {
            pg2sdr_m4clock_wfi();
        }
        m4_wakeup_requested = false;
        Chip_WWDT_Feed(LPC_WWDT);
        __enable_irq();
    }
}

#elif defined(CORE_M0)

volatile bool m0_wakeup_requested;

void pg2sdr_ipc_init()
{
    NVIC_EnableIRQ(M4_IRQn);
}

void M4_IRQHandler(void)
{
    m0_wakeup_requested = true;
    Chip_CREG_ClearM4Event();
    ++pg2sdr_interrupts.m4;
}

bool pg2sdr_ipc_send_m4(m4_ipc_message_type_t message, uint32_t value0, uint32_t value1, uint32_t value2)
{
    if (!IS_M4_MESSAGE(message))
        return false;
    bool result;
    WITH_DISABLED_INTERRUPTS {
        result = ipc_send(m0_to_m4_mailbox, message, value0, value1, value2);
        if (result) {
            shared_memory_barrier(); // Ensure that the queue changes have really hit memory before signaling the other core
            __SEV();
        }
    }
    return result;
}

bool pg2sdr_ipc_send_m0(m0_ipc_message_type_t message, uint32_t value0, uint32_t value1, uint32_t value2)
{
    if (!IS_M0_MESSAGE(message))
        return false;
    bool result;
    WITH_DISABLED_INTERRUPTS {
        result = ipc_send(m0_to_m0_mailbox, message, value0, value1, value2);
        if (result)
            m0_wakeup_requested = true;
    }
    return result;
}

void pg2sdr_ipc_handle_messages_forever(ipc_message_handler_t handler)
{
    while (true) {
        ipc_receive(m4_to_m0_mailbox, handler);
        ipc_receive(m0_to_m0_mailbox, handler);
        __disable_irq();
        if (!m0_wakeup_requested)
            __WFI();
        m0_wakeup_requested = false;
        __enable_irq();
    }
}

#else
#  error Define either CORE_M4 or CORE_M0
#endif

