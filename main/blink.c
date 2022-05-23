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

#define INNER_BARRIER_GPIO CONFIG_LIGHT1_GPIO // light 1 -> 33
#define OUTER_BARRIER_GPIO CONFIG_LIGHT2_GPIO // light 2 -> 34
#define INNER_BARRIER_ADC ADC_CHANNEL_5
#define OUTER_BARRIER_ADC ADC_CHANNEL_6

static const char *TAG = "BLINK";
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
    // This means the sequence must follow: outer (1) -> inner (2) -> outer (1) -> outer (2)
    while (1) {
        vTaskDelay(100);
    }
 }

void IRAM_ATTR decrementTask(void* params) {
    // Expect to decrement
    // This means the sequence must follow: inner (2) -> outer (1) -> inner (2) -> outer (1)
    while (1) {
        vTaskDelay(100);
    }
}

void IRAM_ATTR outerBarrierIsr(void) {
    ets_printf("OUTER %d\n", adc1_get_raw(OUTER_BARRIER_ADC));
}

void IRAM_ATTR innerBarrierIsr(void) {
    ets_printf("INNER %d\n", adc1_get_raw(INNER_BARRIER_ADC));
}

void app_main(void){
    esp_log_level_set(TAG, ESP_LOG_INFO);

    /* HW Setup */

    ESP_ERROR_CHECK(gpio_set_direction(OUTER_BARRIER_GPIO, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_direction(OUTER_BARRIER_GPIO, GPIO_MODE_INPUT));

    /* Interrupts */

    gpio_install_isr_service(0);
 	gpio_set_intr_type(OUTER_BARRIER_GPIO, GPIO_INTR_ANYEDGE);
 	gpio_isr_handler_add(OUTER_BARRIER_GPIO, outerBarrierIsr, NULL);
 	gpio_set_intr_type(INNER_BARRIER_GPIO, GPIO_INTR_ANYEDGE);
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
