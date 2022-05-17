#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ssd1306.h"

#define OUTER_BARRIER 1
#define OUTER_BARRIER_GPIO CONFIG_LIGHT1_GPIO
#define INNER_BARRIER 2
#define INNER_BARRIER_GPIO CONFIG_LIGHT2_GPIO
#define DEBOUNCE_TIME_IN_MICROSECONDS 1000 * 100 // in miliseconds

#define MEASURE_TICK_INTERVAL_MODE 1
#define MEASURE_ISR_INTERVAL_MODE 2

static const char *TAG = "BLINK";
static const char *OUTER = "OUT";
static const char *INNER = "IN ";
static const uint8_t mode = MEASURE_ISR_INTERVAL_MODE;

volatile uint8_t count = 0;
volatile uint64_t lastInterruptTs, lastStableOuterTs, lastStableInnerTs;
static TaskHandle_t taskHandle1;
static TaskHandle_t taskHandle2;

void initDisplay() {
    ssd1306_128x32_i2c_init();
    ssd1306_setFixedFont(ssd1306xled_font6x8);
    ssd1306_clearScreen();
}

void IRAM_ATTR showRoomState(void) {
    char buffer[1];
    ssd1306_clearScreen();
 	itoa((int) count, buffer, 10); // Convert int to char*
	ssd1306_printFixedN(96, 0, "G-10", STYLE_NORMAL, 0.5);
    ssd1306_printFixedN(0, 0, "Count:", STYLE_NORMAL, 1);
 	ssd1306_printFixedN(0, 16, buffer, STYLE_NORMAL, 1);
}

/*
 * Function to print out the esp_timer_get_time() interval for the duration of 100 ticks.
 */
void IRAM_ATTR measureTickInterval(void) {
    uint64_t currentTs = 0;
    while (1) {
        currentTs = esp_timer_get_time();
        ets_printf("100 ticks = %d microseconds\n", (int) (currentTs - lastInterruptTs));
        lastInterruptTs = currentTs;
        count++;
        vTaskDelay(100);
    }
}

/* 
 * FOR TEST PURPOSE ONLY. COMPLETE WALK THROUGH WITHINN 3 SECONDS.
 * Use this to understand the critical time (i.e. time taken to trigger another barrier after the first is broken. 
 */
void IRAM_ATTR measureIsrInterval(int barrier) {
    uint64_t currentTime = esp_timer_get_time();
    uint64_t lastStableTs = 0;
    uint64_t lastInterruptInterval = 0;
    char* barrierName = "";
    
    switch (barrier)
    {
    case OUTER_BARRIER:
        lastStableTs  = lastStableOuterTs;
        barrierName = OUTER;
        break;
    
    case INNER_BARRIER:
        lastStableTs = lastStableInnerTs;
        barrierName = INNER;
        break;

    default:
        break;
    }

    if (currentTime - lastStableTs > DEBOUNCE_TIME_IN_MICROSECONDS) {
        lastInterruptInterval = currentTime - lastInterruptTs;
        if (lastInterruptInterval > 1000 * 3000) lastInterruptInterval = 0; // Reset if it takes too long (i.e. > 1 sec) 

        ets_printf("%s: %d (+%d microseconds)\n", barrierName, (int) currentTime, (int) lastInterruptInterval);
        switch (barrier)
        {
        case OUTER_BARRIER:
            lastStableOuterTs = currentTime;
            break;

        case INNER_BARRIER:
            lastStableInnerTs = currentTime;
            break;

        default:
            break;
        }

        lastInterruptTs = currentTime;
    } else {
        ets_printf("DEBOUNCE\n");
    }

}

void IRAM_ATTR outerBarrierIsr(void) {
    // TODO
}

void IRAM_ATTR innerBarrierIsr(void) {
    // TODO
}

void app_main(void){
    esp_log_level_set(TAG, ESP_LOG_INFO);

    /* HW Setup */

    ESP_ERROR_CHECK(gpio_set_direction(OUTER_BARRIER_GPIO, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_direction(INNER_BARRIER_GPIO, GPIO_MODE_INPUT));

    /* Interrupts */

    gpio_install_isr_service(0);
 	gpio_set_intr_type(OUTER_BARRIER_GPIO, GPIO_INTR_NEGEDGE);
 	gpio_set_intr_type(INNER_BARRIER_GPIO, GPIO_INTR_NEGEDGE);

    /* Tasks */

    switch (mode)
    {
    case MEASURE_TICK_INTERVAL_MODE:
        xTaskCreatePinnedToCore(
            measureTickInterval,
            "Measure tick interval",
            2048,
            NULL,
            1,
            &taskHandle1,
            0
        );
        break;
    
    case MEASURE_ISR_INTERVAL_MODE:
        gpio_isr_handler_add(OUTER_BARRIER_GPIO, measureIsrInterval, OUTER_BARRIER);
        gpio_isr_handler_add(INNER_BARRIER_GPIO, measureIsrInterval, INNER_BARRIER);
        break;

    default:
        break;
    }

    /* Display */

    initDisplay();
    showRoomState();
}
