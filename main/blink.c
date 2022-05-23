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
#define NEGEDGE_THRESHOLD_RAW 2270 // Anything above 2000 is a neg edge
#define TASK_TIMEOUT_IN_MICROSECONDS 1000 * 1000 * 3 

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
    uint64_t wakeTs = 0;
    uint32_t ulNotification = 0;
    bool outerTriggered = false;

    while (1) {
        if (!ulNotification) {
            xTaskNotifyWait(
                ULONG_MAX,
                ULONG_MAX,
                &ulNotification,
                portMAX_DELAY
            );
            ets_printf("Wake %d\n", ulNotification);
        } else {
            if (esp_timer_get_time() - wakeTs > TASK_TIMEOUT_IN_MICROSECONDS) {
                // If timeout
                outerTriggered = false;
            }

            switch (ulNotification)
            {
            case 1:
                ets_printf("Increment ACTIVE\n");
                outerTriggered = true;
                wakeTs = esp_timer_get_time();
                break;
            
            case 2:
                if (outerTriggered) {
                    ets_printf("Increment MATCH\n");
                    count++;
                    printToDisplay();
                    xTaskNotify(
                        innerBarrierTaskHandle,
                        3,
                        eSetBits
                    );
                } else {
                    break;
                }
                // Continues to case 3

            case 3:
                // Reset case called by complementary task
                // or by timeout

                ets_printf("Increment RESET\n");
                outerTriggered = false;
                break;

            default:
                break;
            }

            ulNotification = 0;
        }
    }
 }

void IRAM_ATTR decrementTask(void* params) {
    // Expect to decrement
    uint64_t wakeTs = 0;
    uint32_t ulNotification = 0;
    bool isActive = false;

    while (1) {
        if (!ulNotification) {
            xTaskNotifyWait(
                ULONG_MAX,
                ULONG_MAX,
                &ulNotification,
                portMAX_DELAY
            );
        } else {
            if (esp_timer_get_time() - wakeTs > TASK_TIMEOUT_IN_MICROSECONDS) {
                // If timeout
                isActive = false;
            }

            switch (ulNotification)
            {
            case 2:
                ets_printf("Decrement ACTIVE\n");
                isActive = true;
                wakeTs = esp_timer_get_time();
                break;
            
            case 1:
                if (isActive) {
                    ets_printf("Decrement MATCH\n");
                    if (count > 0) count--;
                    printToDisplay();
                } else {
                    break;
                }
                // xTaskNotify(
                //     outerBarrierTaskHandle,
                //     3,
                //     eSetBits
                // );
                // Continues to case 3

            case 3:
                // Reset case called by complementary task
                // or by timeout

                ets_printf("Decrement RESET\n");
                isActive = false;
                break;

            default:
                break;
            }

            ulNotification = 0;
        }
    }
}

void IRAM_ATTR outerBarrierIsr(void) {
    uint32_t raw = adc1_get_raw(OUTER_BARRIER_ADC);
    if (raw > NEGEDGE_THRESHOLD_RAW) {
        ets_printf("OUTER %d\n", (int) raw);
        xTaskNotifyFromISR(
            outerBarrierTaskHandle,
            1,
            eSetBits,
            NULL
        );
        xTaskNotifyFromISR(
            innerBarrierTaskHandle,
            1,
            eSetBits,
            NULL
        );
    }
}

void IRAM_ATTR innerBarrierIsr(void) {
    uint32_t raw = adc1_get_raw(INNER_BARRIER_ADC);
    if (raw > NEGEDGE_THRESHOLD_RAW) {
        ets_printf("INNER %d\n", (int) raw);
        xTaskNotifyFromISR(
            outerBarrierTaskHandle,
            2,
            eSetBits,
            NULL
        );
        xTaskNotifyFromISR(
            innerBarrierTaskHandle,
            2,
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
