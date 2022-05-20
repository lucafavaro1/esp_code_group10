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
#define TASK_TIMEOUT_IN_MICROSEC 1000 * 3000 // 3s


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
    // This means the sequence must follow: outer (1) -> inner (2) -> outer (1) -> outer (2)
    const int modelSequence[4] = [1, 2, 1, 2];
    uint64_t wakeTime = 0;
    uint32_t pulNotificationValue = 0;
    int sequenceArray[4] = []
    int sequenceCount = 0;
    bool canIncrement = true;
    while (1) {
        if (!pulNotificationValue) {
            xTaskNotifyWait(
                ULONG_MAX,
                ULONG_MAX,
                &pulNotificationValue,
                portMAX_DELAY
            );
        } else {
            if (pulNotificationValue == -1 || esp_timer_get_time() - wakeTime > TASK_TIMEOUT_IN_MICROSEC) {
                sequenceCount = 0;
            }
            switch(pulNotificationValue) {
            case -1: // Forced to reset by other task
                if (count == 2) sequenceCount = 0; // Only reset if other task has state [2, 1, 2, x]
                break;
            default:
                sequenceArray[sequenceCount] = pulNotificationValue;
                sequenceCount++;
                break;
            }

            // Check if criteria matches
            canIncrement = true;
            if (sequenceCount == 3) {
                for (int i = 0; i < sequenceCount; i++) {
                    if (sequenceArray[i] != modelSequence[i]) {
                        canIncrement = false;
                    }
                    if (canDecrement && i == 3) {
                        count++;
                        sequenceCount = 0;
                        xTaskNotify(
                            innerBarrierTaskHandle,
                            -1,
                            eSetBits,
                            NULL);
                    }
                }
            }

            pulNotificationValue = 0; // Put the task to sleep
        }
    }
 }

void IRAM_ATTR decrementTask(void* params) {
    // Expect to decrement
    // This means the sequence must follow: inner (2) -> outer (1) -> inner (2) -> outer (1)
    const int modelSequence[4] = [2, 1, 2, 1];
    uint64_t wakeTime = 0;
    uint32_t pulNotificationValue = 0;
    int sequenceArray[4] = []
    int sequenceCount = 0;
    bool canDecrement = true;
    while (1) {
        if (!pulNotificationValue) {
            xTaskNotifyWait(
                ULONG_MAX,
                ULONG_MAX,
                &pulNotificationValue,
                portMAX_DELAY
            );
        } else {
            if (pulNotificationValue == -1 || esp_timer_get_time() - wakeTime > TASK_TIMEOUT_IN_MICROSEC) {
                sequenceCount = 0;
            }
            switch(pulNotificationValue) {
            case -1: // Forced to reset by other task
                if (count == 2) sequenceCount = 0; // Only reset if other task has state [1, 2, 1, x]
                break;
            default:
                sequenceArray[sequenceCount] = pulNotificationValue;
                sequenceCount++;
                break;
            }

            // Check if criteria matches
            canDecrement = true;
            if (sequenceCount == 3) {
                for (int i = 0; i < sequenceCount; i++) {
                    if (sequenceArray[i] != modelSequence[i]) {
                        canDecrement = false;
                    }
                    if (canDecrement && i == 3) {
                        if (count > 0) count--;
                        sequenceCount = 0;
                        xTaskNotify(
                            outerBarrierTaskHandle,
                            -1,
                            eSetBits,
                            NULL);
                    }
                }
            }

            pulNotificationValue = 0; // Put the task to sleep
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

        xTaskNotifyFromISR(
            innerBarrierTaskHandle,
            1,
            eSetBits,
            NULL);

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

        xTaskNotifyFromISR(
            outerBarrierTaskHandle,
            2,
            eSetBits,
            NULL);

        xTaskNotifyFromISR(
            innerBarrierTaskHandle,
            2,
            eSetBits,
            NULL);

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
