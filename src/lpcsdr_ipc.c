#include "lpcsdr_ipc.h"

#include <string.h>
#include "chip.h"

void lpcsdr_ipc_receive(ipc_mailbox_t *mailbox, ipc_message_handler_t handler)
{
    // nb: no mutual exclusion on the reader side, it's assumed there
    // is only a single reader
    uint32_t head = mailbox->head;
    while (head != mailbox->tail) {
        ipc_message_t message = mailbox->queue[head];
        mailbox->head = head = (head + 1) % IPC_MAILBOX_SIZE;
        __DMB();
        handler(&message);
    }
}

bool lpcsdr_ipc_pending(ipc_mailbox_t *mailbox)
{
    return (mailbox->head != mailbox->tail);
}

#if defined(CORE_M4)

volatile bool m4_wakeup_requested;

void lpcsdr_ipc_init()
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
}

static bool ipc_send(ipc_mailbox_t *mailbox, uint32_t message, uint32_t value0, uint32_t value1, uint32_t value2)
{
    // M4 has LDREX/STREX for isolation on a single core
    uint32_t tail, next;
    do {
        tail = __LDREXW(&mailbox->tail);
        next = (tail + 1) % IPC_MAILBOX_SIZE;
        if (next == mailbox->head) {
            return false;
        }

        mailbox->queue[tail].message = message;
        mailbox->queue[tail].values[0] = value0;
        mailbox->queue[tail].values[1] = value1;
        mailbox->queue[tail].values[2] = value2;
        __DMB();
    } while (__STREXW(next, &mailbox->tail));

    return true;
}

bool lpcsdr_ipc_send_m4(m4_ipc_message_type_t message, uint32_t value0, uint32_t value1, uint32_t value2)
{
    if (!IS_M4_MESSAGE(message))
        return false;
    bool result = ipc_send(m4_to_m4_mailbox, message, value0, value1, value2);
    if (result)
        m4_wakeup_requested = true;
    return result;
}

bool lpcsdr_ipc_send_m0(m0_ipc_message_type_t message, uint32_t value0, uint32_t value1, uint32_t value2)
{
    if (!IS_M0_MESSAGE(message))
        return false;
    bool result = ipc_send(m4_to_m0_mailbox, message, value0, value1, value2);
    if (result)
        __SEV();
    return result;
}

void lpcsdr_ipc_handle_messages_forever(ipc_message_handler_t handler)
{
    while (true) {
        lpcsdr_ipc_receive(m4_to_m4_mailbox, handler);
        lpcsdr_ipc_receive(m0_to_m4_mailbox, handler);
        __disable_irq();
        if (!m4_wakeup_requested)
            __WFI();
        m4_wakeup_requested = false;
        __enable_irq();
    }
}

#elif defined(CORE_M0)

volatile bool m0_wakeup_requested;

void lpcsdr_ipc_init()
{
    NVIC_EnableIRQ(M4_IRQn);
}

void M4_IRQHandler(void)
{
    m0_wakeup_requested = true;
    Chip_CREG_ClearM4Event();
}

static bool ipc_send(ipc_mailbox_t *mailbox, uint32_t message, uint32_t value0, uint32_t value1, uint32_t value2)
{
    // M0 lacks LDREX/STRX, fully disable interrupts for isolation
    bool success;
    WITH_DISABLED_INTERRUPTS {
        uint32_t tail = mailbox->tail;
        uint32_t next = (tail + 1) % IPC_MAILBOX_SIZE;
        if (next == mailbox->head) {
            success = false;
        } else {
            mailbox->queue[tail].message = message;
            mailbox->queue[tail].values[0] = value0;
            mailbox->queue[tail].values[1] = value1;
            mailbox->queue[tail].values[2] = value2;
            __DMB();
            mailbox->tail = next;
            success = true;
        }
    }

    return success;
}

bool lpcsdr_ipc_send_m4(m4_ipc_message_type_t message, uint32_t value0, uint32_t value1, uint32_t value2)
{
    if (!IS_M4_MESSAGE(message))
        return false;
    bool result = ipc_send(m0_to_m4_mailbox, message, value0, value1, value2);
    if (result)
        __SEV();
    return result;
}

bool lpcsdr_ipc_send_m0(m0_ipc_message_type_t message, uint32_t value0, uint32_t value1, uint32_t value2)
{
    if (!IS_M0_MESSAGE(message))
        return false;
    bool result = ipc_send(m0_to_m0_mailbox, message, value0, value1, value2);
    if (result)
        m0_wakeup_requested = true;
    return result;
}

void lpcsdr_ipc_handle_messages_forever(ipc_message_handler_t handler)
{
    while (true) {
        ipc_receive(m4_to_m0_mailbox, handler);
        ipc_receive(m0_to_m0_mailbox, handler);
        __disable_irq();
        if (!m4_wakeup_requested)
            __WFE();
        m0_wakeup_requested = false;
        __enable_irq();
    }
}

#else
#  error Define either CORE_M4 or CORE_M0
#endif

