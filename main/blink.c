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
#define DEBOUNCE_TIME 0.5

static const char *TAG = "BLINK";

volatile uint8_t count = 0;
bool oldStateLight1 = false, oldStateLight2 = false;
int countLight1 = 0, countLight2 = 0;

int lastFlickerableState1 = LOW, lastFlickerableState2 = LOW;

unsigned long lastDebounceTime1 = 0, lastDebounceTime2 = 0;


static IRAM_ATTR TaskHandle_t taskHandle;

void initDisplay() {
    ssd1306_128x32_i2c_init();
    ssd1306_setFixedFont(ssd1306xled_font6x8);
    ssd1306_clearScreen();
}

void IRAM_ATTR printToDisplay(int number) {
    char buffer[1];
    ssd1306_clearScreen();
 	itoa((int) number, buffer, 10);
 	ssd1306_printFixedN(0, 0, "Count:", STYLE_NORMAL, 1);
	ssd1306_printFixedN(96, 0, "G-10", STYLE_NORMAL, 0.5);
 	ssd1306_printFixedN(0, 16, buffer, STYLE_NORMAL, 1);
}

// Version 1: cross both the sensors to get counted
void IRAM_ATTR showRoomState(void* pvParameters) {
    int lastCount = -1;
    bool stateLight1, stateLight2;

	uint8_t ulNotifiedValue;
    
	while(1) {
        stateLight1 = gpio_get_level(LIGHT1_GPIO);
        stateLight2 = gpio_get_level(LIGHT2_GPIO);

        if (stateLight1 != oldStateLight1 && stateLight1) {
            if (stateLight2) {
                count++;
            }
        }

        if (stateLight2 != oldStateLight2 && stateLight2) {
            if (stateLight1) {
                count--;
            }
        }

		oldStateLight1 = stateLight1;
        oldStateLight2 = stateLight2;

        //ets_printf("Something before");

        if (lastCount != count) {
            lastCount = (int) count;
            printToDisplay(count);
			// xTaskNotifyWait(
			// 	0x00,
			// 	ULONG_MAX,
			// 	&ulNotifiedValue,
			// 	portMAX_DELAY
			// );
        }
		
		//ets_printf("Something after");

    }
}

// Version 2: you cant cross both of them at the same time
// void showRoomState(void* pvParameters) {
//     int lastCount = -1;
//     bool stateLight1, stateLight2;
//     oldStateLight1 = gpio_get_level(LIGHT1_GPIO);
//     oldStateLight2 = gpio_get_level(LIGHT2_GPIO);
//     while(1) {
// 		sense the state of the lights
//         stateLight1 = gpio_get_level(LIGHT1_GPIO);
//         stateLight2 = gpio_get_level(LIGHT2_GPIO);

// 		if(stateLight1 != lastFlickerableState1) {
// 			lastDebounceTime1 = esp_timer_get_time();
// 			lastFlickerableState1 = stateLight1;
// 		}

// 		if(stateLight2 != lastFlickerableState2) {
// 			lastDebounceTime2 = esp_timer_get_time();
// 			lastFlickerableState2 = stateLight2;
// 		}

//         if (stateLight1 != oldStateLight1 && stateLight1) {
//             if((esp_timer_get_time() - lastDebounceTime1) > DEBOUNCE_TIME) 
// 				countLight1++;
// 			if(countLight1!=0 && countLight2!=0) {
// 				count++;
// 				countLight1--;
// 				countLight2--;
// 			}
//         }

//         if (stateLight2 != oldStateLight2 && stateLight2) {
//             if((esp_timer_get_time() - lastDebounceTime2) > DEBOUNCE_TIME)
// 				countLight2++;
// 			if(countLight1!=0 && countLight2!=0) {
// 				if(count>0) 
// 					count--;	
// 				countLight1--;
// 				countLight2--;
// 			}
// 		}

//         if (lastCount != count) {
//             lastCount = (int) count;
//             printToDisplay(count);
//         }

//         oldStateLight1 = stateLight1;
//         oldStateLight2 = stateLight2;

//     }
// }

void IRAM_ATTR light1Handler() {
    //xTaskNotify(taskHandle, 0, eNoAction);
}

void IRAM_ATTR light2Handler() {
    //xTaskNotify(taskHandle, 0, eNoAction);
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
