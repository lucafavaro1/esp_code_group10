#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ssd1306.h"

#define BLINK_GPIO CONFIG_BLINK_GPIO	// led -> 19
#define BUTTON_GPIO CONFIG_BUTTON_GPIO // button -> 23
#define LIGHT1_GPIO CONFIG_LIGHT1_GPIO		// light 1 -> 18
#define LIGHT2_GPIO CONFIG_LIGHT2_GPIO		// light 2 -> 26
#define BLINK_LENGTH CONFIG_BLINK_LENGTH
#define BLINK_FREQ CONFIG_BLINK_FREQ

static const char *TAG = "BLINK";
// static const char *L1 = "LIGHT1";
// static const char *L2 = "LIGHT2";

volatile uint8_t count = 0;
static TaskHandle_t taskHandle;

void initDisplay() {
    ssd1306_128x32_i2c_init();
    ssd1306_setFixedFont(ssd1306xled_font6x8);
    ssd1306_clearScreen();
}

void printToDisplay(int number) {
    char buffer[1];
 	itoa((int) number, buffer, 10);
 	ssd1306_printFixedN(0, 0, "Count:", STYLE_NORMAL, 1);
 	ssd1306_printFixedN(0, 16, buffer, STYLE_NORMAL, 1);
}

void showRoomState(void* pvParameters) {
    uint8_t lastCount = -1;
    while(1) {
        if (lastCount != count) {
            lastCount = count;
            printToDisplay(count);
        }
    }
}

void light1Handler() {
    count++;
}

void light2Handler() {
    count--;
}

void app_main(void){
    esp_log_level_set("BLINK", ESP_LOG_INFO);

    /* HW Setup */

    ESP_ERROR_CHECK(gpio_set_direction(BLINK_GPIO, GPIO_MODE_INPUT_OUTPUT));    // set pin 19 as input and output
    ESP_ERROR_CHECK(gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT));          // set pin 23 as input
    ESP_ERROR_CHECK(gpio_set_direction(LIGHT1_GPIO, GPIO_MODE_INPUT));          // set pin 18 as input
    ESP_ERROR_CHECK(gpio_set_direction(LIGHT2_GPIO, GPIO_MODE_INPUT));          // set pin 26 as input

    /* Display */

    initDisplay();

    xTaskCreate(
        showRoomState,
        "showRoomState",
        1024,
        NULL,
        1,
        taskHandle
    );

    gpio_install_isr_service(0);
 	gpio_set_intr_type(LIGHT1_GPIO, GPIO_INTR_NEGEDGE);
 	gpio_isr_handler_add(LIGHT1_GPIO, light1Handler, NULL);
 	gpio_set_intr_type(LIGHT2_GPIO, GPIO_INTR_NEGEDGE);
 	gpio_isr_handler_add(LIGHT2_GPIO, light2Handler, NULL);
}