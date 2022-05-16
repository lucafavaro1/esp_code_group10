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
#define DEBOUNCE_IN_MICROSEC 10

static const char *TAG = "BLINK";

volatile uint8_t count = 0;
volatile uint64_t outerBarrierLastStableTimestamp, innerBarrierLastStableTimestamp;
static IRAM_ATTR TaskHandle_t outerBarrierTaskHandle;
static IRAM_ATTR TaskHandle_t innerBarrierTaskHandle;

void initDisplay() {
    ssd1306_128x32_i2c_init();
    ssd1306_setFixedFont(ssd1306xled_font6x8);
    ssd1306_clearScreen();
}

void IRAM_ATTR printToDisplay(int number) {
    char buffer[1];
    ssd1306_clearScreen();
 	itoa((int) number, buffer, 10); // Convert int to char*
	ssd1306_printFixedN(96, 0, "G-10", STYLE_NORMAL, 0.5);
    ssd1306_printFixedN(0, 0, "Count:", STYLE_NORMAL, 1);
 	ssd1306_printFixedN(0, 16, buffer, STYLE_NORMAL, 1);
}

void IRAM_ATTR showRoomState(void* pvParameters) {
    // Do something
}

void IRAM_ATTR outerBarrierTask() {
    // Do calculations; expect to increment
    // Call showRoomState()
}

void IRAM_ATTR innerBarrierTask() {
    // Do calculations; expect to decrement
    // TODO: Call showRoomState()
}

void IRAM_ATTR outerBarrierIsr() {
    if (esp_timer_get_time() - outerBarrierLastStableTimestamp > DEBOUNCE_IN_MICROSEC) {
        ets_printf("%d: Outer barrier broken\n", (int) esp_timer_get_time());
        // TODO: Wake outerBarrierTask()
    }
}

void IRAM_ATTR innerBarrierIsr() {
    if (esp_timer_get_time() - innerBarrierLastStableTimestamp > DEBOUNCE_IN_MICROSEC) {
        ets_printf("%d: Inner barrier broken\n", (int) esp_timer_get_time());
        // TODO: Wake innerBarrierTask();
    }
}

void app_main(void){
    esp_log_level_set("BLINK", ESP_LOG_INFO);

    /* HW Setup */

    ESP_ERROR_CHECK(gpio_set_direction(OUTER_BARRIER_GPIO, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_direction(OUTER_BARRIER_GPIO, GPIO_MODE_INPUT));

    /* Interrupts */

    gpio_install_isr_service(0);
 	gpio_set_intr_type(OUTER_BARRIER_GPIO, GPIO_INTR_NEGEDGE);
 	gpio_isr_handler_add(OUTER_BARRIER_GPIO, outerBarrierIsr, NULL);
 	gpio_set_intr_type(INNER_BARRIER_GPIO, GPIO_INTR_NEGEDGE);
 	gpio_isr_handler_add(INNER_BARRIER_GPIO, innerBarrierIsr, NULL);

    /* Display */

    initDisplay();
}
