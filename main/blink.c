#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ssd1306.h"
#include "esp_adc_cal.h"

#define INNER_BARRIER_GPIO CONFIG_LIGHT1_GPIO // light 1 -> 33
#define OUTER_BARRIER_GPIO CONFIG_LIGHT2_GPIO // light 2 -> 34
#define INNER_BARRIER_ADC ADC_CHANNEL_5
#define OUTER_BARRIER_ADC ADC_CHANNEL_6
#define POSEDGE_THRESHOLD_RAW 0 // Anything above 0 is a pos edge (entering movement)
#define NEGEDGE_THRESHOLD_RAW 2270 // Anything above 2270 is a neg edge (leaving movement)

#define TASK_SLEEP 0
#define OUTER_IN 1
#define OUTER_OUT 2
#define INNER_IN 3
#define INNER_OUT 4
#define TASK_RESET 5

static const char *TAG = "BLINK";
static esp_adc_cal_characteristics_t *adc_chars;
volatile uint8_t count = 0;
static IRAM_ATTR TaskHandle_t outerBarrierTaskHandle;
static IRAM_ATTR TaskHandle_t innerBarrierTaskHandle;

void initDisplay() {
    ssd1306_128x32_i2c_init();
    ssd1306_setFixedFont(ssd1306xled_font6x8);
    ssd1306_clearScreen();
}

void IRAM_ATTR printToDisplay(void) {
    char buffer[15];
    ssd1306_clearScreen();
    sprintf(buffer, "%02d", count);
    ssd1306_printFixedN(0, 0, "G10", STYLE_NORMAL, 1);
    ssd1306_printFixedN(68, 0, "00:00", STYLE_NORMAL, 1);
    ssd1306_printFixedN(0, 16, buffer, STYLE_NORMAL, 1);
    ssd1306_printFixedN(104, 16, "00", STYLE_NORMAL, 1);
}

void IRAM_ATTR showRoomState(void) {
    printToDisplay();
}

void IRAM_ATTR incrementTask(void* params) {
    // Expect to increment
    uint32_t ulNotification = 0;
    bool states[5];
    for (int i = 0; i < 5; i++) {
        states[i] = false;
    }    

    while (1) {
        if (ulNotification == TASK_SLEEP) {
            xTaskNotifyWait(
                ULONG_MAX,
                ULONG_MAX,
                &ulNotification,
                portMAX_DELAY
            );
        } else {
            switch (ulNotification)
            {
            case OUTER_IN:
                states[OUTER_IN] = true;
                break;

            case OUTER_OUT:
                if (states[OUTER_IN] && states[INNER_IN]) {
                    states[OUTER_OUT] = true;
                } else {
                    ulNotification = TASK_RESET;
                }
                break;

            case INNER_IN:
                if (states[OUTER_IN]) {
                    states[INNER_IN] = true;
                } else {
                    ulNotification = TASK_RESET;
                }
                break;

            case INNER_OUT:
                if (states[OUTER_IN] && states[INNER_IN] && states[OUTER_OUT]) {
                    count++;
                    printToDisplay();
                } else {
                    ulNotification = TASK_RESET;
                }

            case TASK_RESET:
                // ets_printf("RESET INCREMENT\n");
                for (int i = 0; i < 5; i++) {
                    states[i] = false;
                }
                ulNotification = TASK_SLEEP;
                break;
            
            default:
                break;
            }

            if (ulNotification != TASK_RESET) {
                ulNotification = TASK_SLEEP;
            }
        }
    }
 }

void IRAM_ATTR decrementTask(void* params) {
    // Expect to decrement
    uint32_t ulNotification = 0;
    bool states[5];
    for (int i = 0; i < 5; i++) {
        states[i] = false;
    }    

    while (1) {
        if (ulNotification == TASK_SLEEP) {
            xTaskNotifyWait(
                ULONG_MAX,
                ULONG_MAX,
                &ulNotification,
                portMAX_DELAY
            );
        } else {switch (ulNotification)
            {
            case INNER_IN:
                states[INNER_IN] = true;
                break;

            case INNER_OUT:
                if (states[INNER_IN] && states[OUTER_IN]) {
                    states[INNER_OUT] = true;
                } else {
                    ulNotification = TASK_RESET;
                }
                break;

            case OUTER_IN:
                if (states[INNER_IN]) {
                    states[OUTER_IN] = true;
                } else {
                    ulNotification = TASK_RESET;
                }
                break;

            case OUTER_OUT:
                if (states[INNER_IN] && states[OUTER_IN] && states[INNER_OUT]) {
                    if (count > 0) {
                        count--;
                        printToDisplay();
                    }
                } else {
                    ulNotification = TASK_RESET;
                }

            case TASK_RESET:
                // ets_printf("RESET DECREMENT\n");
                for (int i = 0; i < 5; i++) {
                    states[i] = false;
                }
                ulNotification = TASK_SLEEP;
                break;
            
            default:
                break;
            }

            if (ulNotification != TASK_RESET) ulNotification = TASK_SLEEP;
        }
    }
}

void IRAM_ATTR outerBarrierIsr(void) {
    uint32_t raw = adc1_get_raw(OUTER_BARRIER_ADC);
    ets_printf("OUTER %d\n", raw);
    if (raw > POSEDGE_THRESHOLD_RAW && raw <= NEGEDGE_THRESHOLD_RAW) {
        // ets_printf("OUTER IN\n");
        xTaskNotifyFromISR(
                outerBarrierTaskHandle,
                OUTER_IN,
                eSetBits,
                NULL
            );
        xTaskNotifyFromISR(
            innerBarrierTaskHandle,
            OUTER_IN,
            eSetBits,
            NULL
        );
    } else if (raw > NEGEDGE_THRESHOLD_RAW) {
        // ets_printf("OUTER OUT\n");
        xTaskNotifyFromISR(
            outerBarrierTaskHandle,
            OUTER_OUT,
            eSetBits,
            NULL
        );
        xTaskNotifyFromISR(
            innerBarrierTaskHandle,
            OUTER_OUT,
            eSetBits,
            NULL
        );
    }
}

void IRAM_ATTR innerBarrierIsr(void) {
    uint32_t raw = adc1_get_raw(INNER_BARRIER_ADC);
    ets_printf("INNER %d\n", raw);
    if (raw > POSEDGE_THRESHOLD_RAW && raw <= NEGEDGE_THRESHOLD_RAW) {
        // ets_printf("INNER IN\n");
        xTaskNotifyFromISR(
            outerBarrierTaskHandle,
            INNER_IN,
            eSetBits,
            NULL
        );
        xTaskNotifyFromISR(
            innerBarrierTaskHandle,
            INNER_IN,
            eSetBits,
            NULL
        );
    } else if (raw > NEGEDGE_THRESHOLD_RAW) {
        // ets_printf("INNER OUT\n");
        xTaskNotifyFromISR(
            outerBarrierTaskHandle,
            INNER_OUT,
            eSetBits,
            NULL
        );
        xTaskNotifyFromISR(
            innerBarrierTaskHandle,
            INNER_OUT,
            eSetBits,
            NULL
        );
    }
}

void app_main(void){
    esp_log_level_set(TAG, ESP_LOG_INFO);

    /* HW Setup */

    ESP_ERROR_CHECK(gpio_set_direction(OUTER_BARRIER_GPIO, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_direction(INNER_BARRIER_GPIO, GPIO_MODE_INPUT));

    /* Interrupts */

    gpio_install_isr_service(0);
 	gpio_set_intr_type(OUTER_BARRIER_GPIO, GPIO_INTR_ANYEDGE);
 	gpio_isr_handler_add(OUTER_BARRIER_GPIO, outerBarrierIsr, NULL);
 	gpio_set_intr_type(INNER_BARRIER_GPIO, GPIO_INTR_ANYEDGE);
 	gpio_isr_handler_add(INNER_BARRIER_GPIO, innerBarrierIsr, NULL);

    /* Tasks */

    xTaskCreatePinnedToCore(
        incrementTask,
        "Increment Task",
        4096,
        NULL,
        1,
        &outerBarrierTaskHandle,
        0
    );

    xTaskCreatePinnedToCore(
        decrementTask,
        "Decrement Task",
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
