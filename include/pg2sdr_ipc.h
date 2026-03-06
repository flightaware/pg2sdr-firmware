#ifndef PG2SDR_IPC_H
#define PG2SDR_IPC_H

/*
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

#include "lpc_types.h"

/* More "inter-core" than "inter-process", but this provides
 * a message-passing system between the M4 and M0 cores,
 * and between interrupt handlers and the regular main loop.
 */

/* Messages handled by the M4 main loop */
typedef enum {
    M4_MIN_MESSAGE = 0,
    M4_QUEUE_TEST_DATA,
    M4_UPDATE_POWER_STATE,
    M4_COPY_HSADC_BUFFER,
    M4_USB_EP0_IN,
    M4_USB_EP0_OUT,
    M4_MAX_MESSAGE = 0x7FFFFFFF,
} m4_ipc_message_type_t;

/* Messages handled by the M0 main loop */
typedef enum {
    M0_MIN_MESSAGE = 0x80000000,
    M0_MAX_MESSAGE = 0xFFFFFFFF
} m0_ipc_message_type_t;

#define IS_M4_MESSAGE(x) (((x) & 0x80000000) == 0)
#define IS_M0_MESSAGE(x) (((x) & 0x80000000) != 0)

/* Internal message structure - a message type and 3 opaque 32-bit values */
typedef struct {
    uint32_t message;
    uint32_t values[3];
} ipc_message_t;

/* Storage for one mailbox, with a small ring buffer queue. */
#define IPC_MAILBOX_SIZE 8
typedef struct {
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile ipc_message_t queue[IPC_MAILBOX_SIZE];
} ipc_mailbox_t;

/* Wakeup flag set when a new message arrives for this core */
#ifdef CORE_M4
extern volatile bool m4_wakeup_requested;
#endif

#ifdef CORE_M0
extern volatile bool m0_wakeup_requested;
#endif

/* Handler function implemented in the main loop on each core */
typedef void (*ipc_message_handler_t)(const ipc_message_t *);

/* Do core-specific IPC initialization (mailbox init on the M4 only, interrupt setup on both cores) */
void pg2sdr_ipc_init(void);

/* Consume messages from `mailbox`, and pass each to `handler` for
 * processing. Should only be called from the main loop, and shouldn't
 * be called on the same mailbox from more than one core.
 */
void pg2sdr_ipc_receive(ipc_mailbox_t *mailbox, ipc_message_handler_t handler);

/* Return true if the given mailbox has pending messages */
bool pg2sdr_ipc_pending(ipc_mailbox_t *mailbox);

/* Main loop: run forever, processing messages addressed to this core via `handler` */
void pg2sdr_ipc_handle_messages_forever(ipc_message_handler_t handler) __attribute__(( noreturn ));

/* Shared mailboxes at well-known addresses. There is a separate
 * mailbox for each pair of sending+receiving cores.
 * (Technically, the m4-to-m4 and m0-to-m0 mailboxes don't need
 * well-known addresses, but this is simple enough)
 */
#define SHARED_MAILBOXES ((ipc_mailbox_t *) 0x10008000)
#define m4_to_m0_mailbox (SHARED_MAILBOXES + 0)
#define m4_to_m4_mailbox (SHARED_MAILBOXES + 1)
#define m0_to_m0_mailbox (SHARED_MAILBOXES + 2)
#define m0_to_m4_mailbox (SHARED_MAILBOXES + 3)

/* Send a message to the M4 main loop. Returns true if successfully queued, false if the queue is full.
 * Does not wait for delivery to complete. Safe to call from interrupt handlers and from the main loop.
 */
bool pg2sdr_ipc_send_m4(m4_ipc_message_type_t message, uint32_t value0, uint32_t value1, uint32_t value2);

/* Send a message to the M0 main loop. Returns true if successfully queued, false if the queue is full;
 * does not wait for delivery to complete. Safe to call from interrupt handlers and from the main loop.
 */
bool pg2sdr_ipc_send_m0(m0_ipc_message_type_t message, uint32_t value0, uint32_t value1, uint32_t value2);

#endif /* PG2SDR_IPC_H */
