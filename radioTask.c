/*
 * radioTask.c
 *
 *  Created on: Apr 12, 2019
 *      Author: HaiHui
 */
#include "radioTask.h"
#include <string.h>

/* XDCtools Header files */
#include <xdc/runtime/Assert.h>
#include <xdc/runtime/Error.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Log.h>
#include <xdc/cfg/global.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Swi.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Event.h>
#include <ti/sysbios/knl/Semaphore.h>


/* TI-RTOS Header files */
#include <ti/drivers/PIN.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/UART.h>

/* Board Header files */
#include "Board.h"

#define RFEASYLINKTX_TASK_STACK_SIZE    2048
#define RFEASYLINKTX_TASK_PRIORITY      2

Task_Struct radioTask;
static Task_Params radioTaskParams;
static uint8_t radioTaskStack[RFEASYLINKTX_TASK_STACK_SIZE];


#include "easylink/EasyLink.h"
EasyLink_TxPacket txPacket;

static Event_Struct radioEvent;
static Event_Handle radioEventHandle;
#define RADIO_EVENT_ALL                         0xFFFFFFFF
#define RADIO_EVENT_RECV_PKT                    Event_Id_00
#define RADIO_EVENT_RECV_UART                   Event_Id_01
#define RADIO_EVENT_RADIO_RX_ERROR              Event_Id_02
#define RADIO_EVENT_UART_RX_ERROR               Event_Id_03

/* Pin driver handle */
static PIN_Handle ledPinHandle;
static PIN_State ledPinState;
/*
 * Application LED pin configuration table:
 *   - All LEDs board LEDs are off.
 */
PIN_Config pinTable[] = {
    Board_PIN_LED1 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    Board_PIN_LED2 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
#if defined Board_CC1352R1_LAUNCHXL
    Board_DIO30FSW | PIN_GPIO_OUTPUT_EN | PIN_GPIO_HIGH | PIN_PUSHPULL | PIN_DRVSTR_MAX,
#endif
    PIN_TERMINATE
};
static PIN_Handle pinHandle;



/* async rx done callback */
static void rxDoneCb(EasyLink_RxPacket * rxPacket, EasyLink_Status status);
EasyLink_RxPacket rxPacket_buf;

static void UART_readDoneCb(UART_Handle uh, void *buf, size_t len);

static UART_Handle uartHandle;
static UART_Params uartParams;

/* radio packet */
static EasyLink_TxPacket txPacket;
static Semaphore_Handle txPktMutex;
static EasyLink_RxPacket rxPacket;
static Semaphore_Handle rxPktMutex;

/* UART message */
#define UART_MSG_LENGTH 22
static char uart_msg[UART_MSG_LENGTH];

void radioTask_init(void) {
    /* Open LED pins */
    ledPinHandle = PIN_open(&ledPinState, pinTable);
    Assert_isTrue(ledPinHandle != NULL, NULL);
    pinHandle = ledPinHandle;

    /* Clear LED pins */
    PIN_setOutputValue(ledPinHandle, Board_PIN_LED1, 0);
    PIN_setOutputValue(ledPinHandle, Board_PIN_LED2, 0);

    Event_Params eventParam;
    Event_Params_init(&eventParam);
    Event_construct(&radioEvent, &eventParam);
    radioEventHandle = Event_handle(&radioEvent);

    GPIO_init();
    UART_init();
    UART_Params_init(&uartParams);
    uartParams.writeMode = UART_MODE_BLOCKING;
    uartParams.readMode = UART_MODE_CALLBACK;
    //uartParams.writeCallback = NULL;
    uartParams.readCallback = UART_readDoneCb;
    uartParams.writeDataMode = UART_DATA_BINARY;
    uartParams.readDataMode = UART_DATA_BINARY;
    uartParams.readReturnMode = UART_RETURN_FULL;
    uartParams.readEcho = UART_ECHO_OFF;
    uartParams.baudRate = 115200;

    uartHandle = UART_open(Board_UART0, &uartParams);
    if (uartHandle == NULL) {
        /* UART_open() failed */
        System_abort("UART open failed");
    }
    /* Create a semaphore for Async*/
    Semaphore_Params params;

    /* Init params */
    Semaphore_Params_init(&params);

    /* Create semaphore instance */
    rxPktMutex = Semaphore_create(0, &params, NULL);
    txPktMutex = Semaphore_create(0, &params, NULL);
    if(rxPktMutex == NULL || txPktMutex == NULL)
    {
        System_abort("Semaphore creation failed");
    }
    Semaphore_post(rxPktMutex);
    Semaphore_post(txPktMutex);

    Task_Params_init(&radioTaskParams);
    radioTaskParams.stackSize = RFEASYLINKTX_TASK_STACK_SIZE;
    radioTaskParams.priority = RFEASYLINKTX_TASK_PRIORITY;
    radioTaskParams.stack = &radioTaskStack;

    Task_construct(&radioTask, radioTaskFnx, &radioTaskParams, NULL);
}

void radioTaskFnx(UArg arg0, UArg arg1) {
    /* EasyLink Init */
    EasyLink_Params easyLink_params;
    EasyLink_Params_init(&easyLink_params);

    easyLink_params.ui32ModType = EasyLink_Phy_Custom;

    /* Initialize EasyLink */
    if(EasyLink_init(&easyLink_params) != EasyLink_Status_Success)
    {
        System_abort("EasyLink_init failed");
    }

    EasyLink_Status status;

    uint32_t events;
    status = EasyLink_receiveAsync(rxDoneCb, 0);
    UART_read(uartHandle, uart_msg, UART_MSG_LENGTH);
    while (1) {
        // event 0: receive radio packet
        // event 1: receive UART message
        events  = Event_pend(radioEventHandle, 0, RADIO_EVENT_ALL, BIOS_WAIT_FOREVER);
        if (events & RADIO_EVENT_RECV_PKT) {
            if (Semaphore_pend(rxPktMutex, 0) == FALSE) {
                Semaphore_pend(rxPktMutex, BIOS_WAIT_FOREVER);
            }
            UART_write(uartHandle, rxPacket.payload, rxPacket.len);
            Semaphore_post(rxPktMutex);
            EasyLink_receiveAsync(rxDoneCb, 0);
        }
        if (events & RADIO_EVENT_RECV_UART) {
            status = EasyLink_abort();
            if(status == EasyLink_Status_Success || status == EasyLink_Status_Aborted)
            {
                if (Semaphore_pend(txPktMutex, 0) == FALSE) {
                    Semaphore_pend(txPktMutex, BIOS_WAIT_FOREVER);
                }
                status = EasyLink_transmit(&txPacket);
                Semaphore_post(txPktMutex);
                if (status == EasyLink_Status_Success) {
                    PIN_setOutputValue(pinHandle, Board_PIN_LED1,!PIN_getOutputValue(Board_PIN_LED1));
                }
                else {
                    System_abort("EasyLink_transmit failed");
                }
            }
            UART_read(uartHandle, uart_msg, UART_MSG_LENGTH);
        }
        if (events & RADIO_EVENT_RADIO_RX_ERROR) {
            EasyLink_receiveAsync(rxDoneCb, 0);
        }
        if (events & RADIO_EVENT_UART_RX_ERROR) {
            UART_read(uartHandle, uart_msg, UART_MSG_LENGTH);
        }
    }
}

static void rxDoneCb(EasyLink_RxPacket * rxpkt, EasyLink_Status status)
{
    if (status == EasyLink_Status_Success)
    {
        /* Toggle LED2 to indicate RX */
        PIN_setOutputValue(pinHandle, Board_PIN_LED2,!PIN_getOutputValue(Board_PIN_LED2));
        if (Semaphore_pend(rxPktMutex, 0) == FALSE) {
            Semaphore_pend(rxPktMutex, BIOS_WAIT_FOREVER);
        }
        memcpy(&rxPacket, rxpkt, sizeof(*rxpkt));
        Semaphore_post(rxPktMutex);
        Event_post(radioEventHandle, RADIO_EVENT_RECV_PKT);
        return;
    }
    else if(status == EasyLink_Status_Aborted)
    {
        // RF operation is aborted
    }
    else
    {
        /* Toggle LED1 and LED2 to indicate error */
        // PIN_setOutputValue(pinHandle, Board_PIN_LED1,!PIN_getOutputValue(Board_PIN_LED1));
        // PIN_setOutputValue(pinHandle, Board_PIN_LED2,!PIN_getOutputValue(Board_PIN_LED2));
    }
    Event_post(radioEventHandle, RADIO_EVENT_RADIO_RX_ERROR);
}

static void UART_readDoneCb(UART_Handle uh, void *buf, size_t len) {
    if (len > 0) {
        if (Semaphore_pend(txPktMutex, 0) == FALSE) {
            Semaphore_pend(txPktMutex, BIOS_WAIT_FOREVER);
        }
        txPacket.absTime = 0;
        txPacket.dstAddr[0] = 0xff;
        txPacket.len = len;
        memcpy(txPacket.payload, buf, len);
        Semaphore_post(txPktMutex);
        Event_post(radioEventHandle, RADIO_EVENT_RECV_UART);
    }
    Event_post(radioEventHandle, RADIO_EVENT_UART_RX_ERROR);
}
