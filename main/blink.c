#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "ssd1306.h"

#define BLINK_GPIO CONFIG_BLINK_GPIO	// led -> 19
#define BUTTON_GPIO CONFIG_BUTTON_GPIO // button -> 23
#define LIGHT1_GPIO CONFIG_LIGHT1_GPIO		// light 1 -> 18
#define LIGHT2_GPIO CONFIG_LIGHT2_GPIO		// light 2 -> 26
#define BLINK_LENGTH CONFIG_BLINK_LENGTH
#define BLINK_FREQ CONFIG_BLINK_FREQ

static const char *TAG = "BLINK";
static const char *L1 = "LIGHT1";
static const char *L2 = "LIGHT2";

volatile uint8_t count = 0;


void initDisplay() {
	ssd1306_128x32_i2c_init();
	ssd1306_setFixedFont(ssd1306xled_font6x8);
}

void updateCounter(int count) {
	char buffer[20];
	ssd1306_clearScreen();
	itoa((int) count, buffer, 10);
	ssd1306_printFixedN(0, 0, "Count:", STYLE_NORMAL, 1);
	ssd1306_printFixedN(0, 16, buffer, STYLE_NORMAL, 1);
}

// void tryIncrement(bool state1, bool state2, bool *isIncremented) {
// 	if (state1 && state2 && !*isIncremented) {
// 		count++;
// 		*isIncremented = true;
// 		ESP_LOGI(TAG, "Count: %d", (int) count);
// 		updateCounter(count);
// 	} else if (!state1 && !state2) {
// 		*isIncremented = false;
// 	}
// }

void light1Handler() {
	count++;
	ets_printf("Interrupt %d\n", count);

}

void light2Handler() {
	count--;
	ets_printf("Interrupt %d\n", count);
}

void app_main(void){
	esp_log_level_set("BLINK", ESP_LOG_INFO);

	ESP_ERROR_CHECK(gpio_set_direction(BLINK_GPIO, GPIO_MODE_INPUT_OUTPUT));	// set pin 19 as input and output
	ESP_ERROR_CHECK(gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT));	// set pin 23 as input
	ESP_ERROR_CHECK(gpio_set_direction(LIGHT1_GPIO, GPIO_MODE_INPUT));	// set pin 18 as input
	ESP_ERROR_CHECK(gpio_set_direction(LIGHT2_GPIO, GPIO_MODE_INPUT));	// set pin 26 as input

	ESP_LOGI(TAG, "INTERRUPT %d", (int) gpio_install_isr_service(0));

	gpio_set_intr_type(LIGHT1_GPIO, GPIO_INTR_NEGEDGE);
	gpio_isr_handler_add(LIGHT1_GPIO, light1Handler, NULL);

	gpio_set_intr_type(LIGHT2_GPIO, GPIO_INTR_NEGEDGE);
	gpio_isr_handler_add(LIGHT2_GPIO, light2Handler, NULL);

	// initDisplay();
	// updateCounter((int) count);

	// int oldStateLight1 = gpio_get_level(LIGHT1_GPIO);
	// int oldStateLight2 = gpio_get_level(LIGHT2_GPIO);
	// bool incremented = false;

	// while(1) {
	// 	int stateLight1 = gpio_get_level(LIGHT1_GPIO);
	// 	int stateLight2 = gpio_get_level(LIGHT2_GPIO);

	// 	if (oldStateLight1 != stateLight1) {
	// 		ESP_LOGI(L1, "The state of the barrier 1 changed from %d -> %d", oldStateLight1, stateLight1);
	// 		oldStateLight1 = stateLight1;
	// 	}

	// 	if (oldStateLight2 != stateLight2) {
	// 		ESP_LOGI(L2, "The state of the barrier 2 changed from %d -> %d", oldStateLight2, stateLight2);
	// 		oldStateLight2 = stateLight2;
	// 	}

	// 	tryIncrement(stateLight1, stateLight2, &incremented);

		// vTaskDelay(10); // to avoid "task watchdog got triggered" erro
	// }
}