#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "ssd1306.h"

#define BLINK_GPIO CONFIG_BLINK_GPIO	// led -> 19
#define BUTTON_GPIO CONFIG_BUTTON_GPIO // button -> 23
//#define LIGHT1_GPIO CONFIG_LIGHT1_GPIO		// light 1 -> 18
//#define LIGHT2_GPIO CONFIG_LIGHT2_GPIO		// light 2 -> 26
#define BLINK_LENGTH CONFIG_BLINK_LENGTH
#define BLINK_FREQ CONFIG_BLINK_FREQ

static const char *TAG = "BLINK";
//static const char *L1 = "LIGHT1";
//static const char *L2 = "LIGHT2";


void initDisplay() {
	ssd1306_128x32_i2c_init();
	ssd1306_setFixedFont(ssd1306xled_font6x8);
}

void textDemo() {
	ssd1306_clearScreen();
	ssd1306_printFixedN(0,0, "Example", STYLE_NORMAL,1);
	ssd1306_negativeMode();
	ssd1306_printFixedN(0,16, "Inverted", STYLE_NORMAL,1);
	ssd1306_positiveMode();
	delay(3000);
	ssd1306_clearScreen();
}

void app_main(void){
	//esp_log_level_set("BLINK", ESP_LOG_ERROR);       
	esp_log_level_set("BLINK", ESP_LOG_INFO);       
	
	ESP_ERROR_CHECK(gpio_set_direction(BLINK_GPIO, GPIO_MODE_INPUT_OUTPUT));	// set pin 19 as input and output
	ESP_ERROR_CHECK(gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT));	// set pin 23 as input
	//ESP_ERROR_CHECK(gpio_set_direction(LIGHT1_GPIO, GPIO_MODE_INPUT));	// set pin 18 as input
	//ESP_ERROR_CHECK(gpio_set_direction(LIGHT2_GPIO, GPIO_MODE_INPUT));	// set pin 26 as input

	initDisplay();
	textDemo();

	// while(1) {
	// 	int stateLight1 = gpio_get_level(LIGHT1_GPIO);
	// 	int stateLight2 = gpio_get_level(LIGHT2_GPIO);
	// 	if(stateLight1)
	// 		ESP_LOGI(L1, "Something passing through barrier 1");
	// 	if(stateLight2)
	// 		ESP_LOGI(L2, "Something passing through barrier 2");
	// 	vTaskDelay(10); // to avoid "task watchdog got triggered" erro
	// }

	// // Version 1: one click led on, one click led off 
	// int lastStateButton = gpio_get_level(BUTTON_GPIO);

	// while(1) {
	// 	int currentStateButton = gpio_get_level(BUTTON_GPIO);
	// 	if(!currentStateButton && (currentStateButton != lastStateButton)) { // remember button pressed = level 0 
	// 		int ledState = gpio_get_level(BLINK_GPIO);
	// 		ESP_LOGI(TAG, "Change the LED state");
	// 		ESP_ERROR_CHECK(gpio_set_level(BLINK_GPIO, !ledState));
	// 	}
	// 	lastStateButton = currentStateButton;
	// 	vTaskDelay(10);	// to avoid "task watchdog got triggered" error
	// }
	
	
	// // Version 2: while pressing led on, release button led off
	
	// while(1) {
	// 	int stateButton = gpio_get_level(BUTTON_GPIO);
	// 		if(!stateButton)
	// 			ESP_LOGI(TAG, "Turning on the LED");
	// 		else 
	// 			ESP_LOGI(TAG, "Turning off the LED");

	// 	ESP_ERROR_CHECK(gpio_set_level(BLINK_GPIO, !stateButton));
	// 	vTaskDelay(10); // to avoid "task watchdog got triggered" error
			
	// }
	

	// // version led on and off setting length and frequency in menuconfig
	
	// for(int i=1;i<5;i++) {
    //     // Blink on (output low) 
	// 	ESP_LOGI(TAG, "Turning on the LED");
	// 	ESP_ERROR_CHECK(gpio_set_level(BLINK_GPIO, 1));
	// 	vTaskDelay(1000 * BLINK_LENGTH / portTICK_PERIOD_MS);

    //     // Blink off (output high) 
	// 	ESP_LOGI(TAG, "Turning off the LED");
	// 	ESP_ERROR_CHECK(gpio_set_level(BLINK_GPIO, 0));
	// 	vTaskDelay(1000 * BLINK_FREQ / portTICK_PERIOD_MS);
	// }
	
	// ESP_LOGI(TAG, "Final ON");
	// ESP_ERROR_CHECK(gpio_set_level(BLINK_GPIO, 1));
	// vTaskDelay(1000 / portTICK_PERIOD_MS);

	// ESP_LOGI(TAG, "Final OFF");
	// ESP_ERROR_CHECK(gpio_set_level(BLINK_GPIO, 0));
	
}

