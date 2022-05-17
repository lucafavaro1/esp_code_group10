#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ssd1306.h"

#define INNER_BARRIER_GPIO CONFIG_LIGHT1_GPIO // light 1 -> 18
#define OUTER_BARRIER_GPIO CONFIG_LIGHT2_GPIO // light 2 -> 26
#define DEBOUNCE_IN_MICROSEC 20 // 10ms  
// #define TASK_TIMEOUT_IN_MICROSEC 3e+6 // 3s
#define TASK_TIMEOUT_IN_MICROSEC 3e+6 // 3s


static const char *TAG = "BLINK";

volatile uint8_t count = 0;
volatile uint64_t outerBarrierLastStableTimestamp = 0, innerBarrierLastStableTimestamp = 0;
static IRAM_ATTR TaskHandle_t outerBarrierTaskHandle;
static IRAM_ATTR TaskHandle_t innerBarrierTaskHandle;

void initDisplay() {
    ssd1306_128x32_i2c_init();
    ssd1306_setFixedFont(ssd1306xled_font6x8);
    ssd1306_clearScreen();
}

void IRAM_ATTR printToDisplay(void) {
    char buffer[1];
    ssd1306_clearScreen();
 	itoa((int) count, buffer, 10); // Convert int to char*
	ssd1306_printFixedN(96, 0, "G-10", STYLE_NORMAL, 0.5);
    ssd1306_printFixedN(0, 0, "Count:", STYLE_NORMAL, 1);
 	ssd1306_printFixedN(0, 16, buffer, STYLE_NORMAL, 1);
}

void IRAM_ATTR showRoomState(void) {
    printToDisplay();
}

void IRAM_ATTR incrementTask(void* params) {
    // Expect to increment

    uint32_t triggerTimestamp = 0;
    uint32_t pulNotificationValue = 0;
    uint32_t initPul = 0;
    bool selfBlock = false;
    bool selfRestore = false;
    bool peerBlock = false;
    bool peerRestore = false;
    bool reset = false;
    uint32_t start = 0;

    while (1) {
        if (pulNotificationValue == 0) {
            // ets_printf("Increment task sleeping..\n");
            xTaskNotifyWait(
                ULONG_LONG_MAX,
                ULONG_LONG_MAX,
                &pulNotificationValue,
                portMAX_DELAY
            );
            // ets_printf("Increment task awake..\n");
        } else {
            if (!reset) reset = esp_timer_get_time() - triggerTimestamp > TASK_TIMEOUT_IN_MICROSEC;

            if (reset) {
                initPul = pulNotificationValue;
                selfBlock = false;
                selfRestore = false;
                peerBlock = false;
                peerRestore = false;
                reset = false;
                ets_printf("%d\n", pulNotificationValue);
            }

            switch (pulNotificationValue)
            {
                case 1:
                    if (!selfBlock) {
                        selfBlock = true;
                        triggerTimestamp = esp_timer_get_time();
                    } else if (!selfRestore && initPul == 1) {
                        selfRestore = true;
                    }
                    break;
                case 2:
                    if (!peerBlock) {
                        peerBlock = true;
                    } else if (!peerRestore) {
                        if (initPul == 1) {
                            peerRestore = true;
                        } else {
                            triggerTimestamp = 0;
                        }
                    }
                    break;
                default:
                    break;
            }

            ets_printf("%d, %d, %d, %d\n", selfBlock, selfRestore, peerBlock, peerRestore);

            if (selfBlock && selfRestore && peerBlock && peerRestore) {
                reset = true;
                count++;
                showRoomState();
                ets_printf("+\n");
            }

            if (!reset) pulNotificationValue = 0; // Put to sleep again
        }
    }
 }

void IRAM_ATTR decrementTask(void* params) {
    /// Expect to decrement

    uint32_t triggerTimestamp = 0;
    uint32_t pulNotificationValue = 0;
    bool selfBlock = false;
    bool selfRestore = false;
    bool peerBlock = false;
    bool peerRestore = false;
    bool increment = false;

    while (1) {
        if (pulNotificationValue == 0) {
            // ets_printf("Increment task sleeping..\n");
            xTaskNotifyWait(
                ULONG_LONG_MAX,
                ULONG_LONG_MAX,
                &pulNotificationValue,
                portMAX_DELAY
            );
            // ets_printf("Increment task awake..\n");
        } else {
            if (increment || esp_timer_get_time() - triggerTimestamp > TASK_TIMEOUT_IN_MICROSEC) {
                selfBlock = false;
                selfRestore = false;
                peerBlock = false;
                peerRestore = false;
                increment = false;
            }

            switch (pulNotificationValue)
            {
                case 2:
                    if (!selfBlock) {
                        selfBlock = true;
                        triggerTimestamp = esp_timer_get_time();
                    } else if (!selfRestore) {
                        selfRestore = true;
                    }
                    break;
                case 1:
                    if (selfBlock && !peerBlock) {
                        peerBlock = true;
                    }
                    break;
                default:
                    break;
            }

            // ets_printf("%d, %d, %d, %d\n", selfBlock, selfRestore, peerBlock, peerRestore);

            if (selfBlock && selfRestore && peerBlock) {
                increment = true;
                if (count > 0) count--;
                showRoomState();
                // ets_printf("-");
            }
            
            pulNotificationValue = 0; // Put to sleep again
        }
    }
}

void IRAM_ATTR outerBarrierIsr(void) {
    uint64_t currentTime = esp_timer_get_time();
    if (currentTime - outerBarrierLastStableTimestamp > DEBOUNCE_IN_MICROSEC) { // Can replace with xTaskNotifyClear() if available
        xTaskNotifyFromISR(
            outerBarrierTaskHandle, 
            1, 
            eSetBits, 
            NULL);
        portYIELD_FROM_ISR();
        
        // xTaskNotifyFromISR(
        //     innerBarrierTaskHandle, 
        //     1, 
        //     eSetBits, 
        //     NULL);
        // portYIELD_FROM_ISR();

        outerBarrierLastStableTimestamp = currentTime;
    } else {
        ets_printf("DEBOUNCED\n");
    }
}

void IRAM_ATTR innerBarrierIsr(void) {
    uint64_t currentTime = esp_timer_get_time();
    if (currentTime - innerBarrierLastStableTimestamp > DEBOUNCE_IN_MICROSEC) {
        xTaskNotifyFromISR(
            outerBarrierTaskHandle, 
            2, 
            eSetBits, 
            NULL);
        portYIELD_FROM_ISR();

        // xTaskNotifyFromISR(
        //     innerBarrierTaskHandle, 
        //     2, 
        //     eSetBits, 
        //     NULL);
        // portYIELD_FROM_ISR();

        innerBarrierLastStableTimestamp = currentTime;
    }
}

void app_main(void){
    esp_log_level_set(TAG, ESP_LOG_INFO);

    /* HW Setup */

    ESP_ERROR_CHECK(gpio_set_direction(OUTER_BARRIER_GPIO, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_direction(OUTER_BARRIER_GPIO, GPIO_MODE_INPUT));

    /* Interrupts */

    gpio_install_isr_service(0);
 	gpio_set_intr_type(OUTER_BARRIER_GPIO, GPIO_INTR_NEGEDGE);
 	gpio_isr_handler_add(OUTER_BARRIER_GPIO, outerBarrierIsr, NULL);
 	gpio_set_intr_type(INNER_BARRIER_GPIO, GPIO_INTR_NEGEDGE);
 	gpio_isr_handler_add(INNER_BARRIER_GPIO, innerBarrierIsr, NULL);

    /* Tasks */

    xTaskCreatePinnedToCore(
        incrementTask,
        "Outer Barrier Task",
        4096,
        NULL,
        1,
        &outerBarrierTaskHandle,
        0
    );

    xTaskCreatePinnedToCore(
        decrementTask,
        "Inner Barrier Task",
        4096,
        NULL,
        1,
        &innerBarrierTaskHandle,
        1
    );

    /* Display */

    initDisplay();
    showRoomState();
}
