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
#define DEBOUNCE_TIME_IN_MICROSECONDS 300 * 1000   // in miliseconds
#define TASK_WAIT_TIME_IN_MICROSECONDS 1000 * 1000 // in miliseconds

static const char *OUTER = "OUT";
static const char *INNER = "IN ";


volatile uint8_t count = 0;
volatile uint64_t lastInterruptTs = 0;
volatile uint64_t lastStableOuterTs = 0;
volatile uint64_t lastStableInnerTs = 0;
volatile bool lockOuterTask = false;
volatile bool lockInnerTask = false;
static TaskHandle_t taskHandle1;
static TaskHandle_t taskHandle2;

void initDisplay()
{
    ssd1306_128x32_i2c_init();
    ssd1306_setFixedFont(ssd1306xled_font6x8);
    ssd1306_clearScreen();
}

void showRoomState(void)
{
    char buffer[1];
    ssd1306_clearScreen();
    itoa((int)count, buffer, 10); // Convert int to char*
    ssd1306_printFixedN(96, 0, "G-10", STYLE_NORMAL, 0.5);
    ssd1306_printFixedN(0, 0, "Count:", STYLE_NORMAL, 1);
    ssd1306_printFixedN(0, 16, buffer, STYLE_NORMAL, 1);
}

void outerTask(void)
{
    uint32_t barrier = 0;
    uint64_t wakeTime = 0;
    bool oldStateOuter, oldStateInner;
    bool waitingForRelease = false;
    bool stateTimeline[8]; // first outer, then inner

    while (1)
    {
        if (barrier == 0)
        {
            ets_printf("%s task sleeping..\n", OUTER);
            lockOuterTask = false;
            waitingForRelease = false;
            xTaskNotifyWait(
                ULONG_MAX,
                ULONG_MAX,
                &barrier,
                portMAX_DELAY);
            lockOuterTask = true;
            wakeTime = esp_timer_get_time();
            ets_printf("%s barrier interrupted with value %d\n", OUTER, barrier);
        }
        else
        {
            stateTimeline[0] = 1;
            stateTimeline[1] = gpio_get_level(INNER_BARRIER_GPIO);
            oldStateOuter = stateTimeline[0];
            oldStateInner = stateTimeline[1];
            int i = 2;

            while (esp_timer_get_time() - wakeTime < TASK_WAIT_TIME_IN_MICROSECONDS && i < 8)
            {
                int currentOuter = gpio_get_level(OUTER_BARRIER_GPIO);
                int currentInner = gpio_get_level(INNER_BARRIER_GPIO);
                if (currentOuter != oldStateOuter)
                {
                    oldStateOuter = currentOuter;
                    stateTimeline[i] = oldStateOuter;
                    stateTimeline[i + 1] = oldStateInner;
                    i = i + 2;
                }
                else if (currentInner != oldStateInner)
                {
                    oldStateInner = currentInner;
                    stateTimeline[i] = oldStateOuter;
                    stateTimeline[i + 1] = oldStateInner;
                    i = i + 2;
                }
            }

            ets_printf("OUTER: Before printing the array (right is 1 0 1 1 0 1 0 0) \n");

            for (int j = 0; j < 8; j = j + 2)
            {
                ets_printf("[%d, %d]", stateTimeline[j], stateTimeline[j + 1]);
            }

            bool rightTimeline[8] = {1, 0, 1, 1, 0, 1, 0, 0};
            int counterEquals = 0;

            for (int j = 0; j < 8; j++)
            {
                if (stateTimeline[j] == rightTimeline[j])
                    counterEquals++;
            }

            if (counterEquals == 8)
            {
                count++;
                showRoomState();
            }
            barrier = 0;

            vTaskDelay(10); // Wait 10 ticks
        }
    }
}

void innerTask(void)
{
    uint32_t barrier = 0;
    uint64_t wakeTime = 0;
    bool oldStateOuter, oldStateInner;
    bool waitingForRelease = false;
    bool stateTimeline[8]; // first outer, then inner

    while (1)
    {
        if (barrier == 0)
        {
            ets_printf("%s task sleeping..\n", INNER);
            lockInnerTask = false;
            waitingForRelease = false;
            xTaskNotifyWait(
                ULONG_MAX,
                ULONG_MAX,
                &barrier,
                portMAX_DELAY);
            lockInnerTask = true;
            wakeTime = esp_timer_get_time();
            ets_printf("%s barrier interrupted with value %d\n", INNER, barrier);
        }
        else
        {
            stateTimeline[0] = gpio_get_level(OUTER_BARRIER_GPIO);
            stateTimeline[1] = 1;
            oldStateOuter = stateTimeline[0];
            oldStateInner = stateTimeline[1];
            int i = 2;

            while (esp_timer_get_time() - wakeTime < TASK_WAIT_TIME_IN_MICROSECONDS && i < 8)
            {
                int currentOuter = gpio_get_level(OUTER_BARRIER_GPIO);
                int currentInner = gpio_get_level(INNER_BARRIER_GPIO);
                if (currentOuter != oldStateOuter)
                {
                    oldStateOuter = currentOuter;
                    stateTimeline[i] = oldStateOuter;
                    stateTimeline[i + 1] = oldStateInner;
                    i = i + 2;
                }
                else if (currentInner != oldStateInner)
                {
                    oldStateInner = currentInner;
                    stateTimeline[i] = oldStateOuter;
                    stateTimeline[i + 1] = oldStateInner;
                    i = i + 2;
                }
            }

            ets_printf("INNER: Before printing the array (right is 0 1 1 1 1 0 0 0) \n");

            for (int j = 0; j < 8; j = j + 2)
            {
                ets_printf("[%d, %d]", stateTimeline[j], stateTimeline[j + 1]);
            }

            bool rightTimeline[8] = {0, 1, 1, 1, 1, 0, 0, 0};
            int counterEquals = 0;

            for (int j = 0; j < 8; j++)
            {
                if (stateTimeline[j] == rightTimeline[j])
                    counterEquals++;
            }

            if (counterEquals == 8)
            {
                if (count > 0)
                    count--;
                showRoomState();
            }
            barrier = 0;

            vTaskDelay(10); // Wait 10 ticks
        }
    }
}

void IRAM_ATTR outerIsr(void)
{
    uint64_t currentTs = esp_timer_get_time();
    if (currentTs - lastStableOuterTs > DEBOUNCE_TIME_IN_MICROSECONDS && !lockOuterTask)
    {
        xTaskNotifyFromISR(
            taskHandle1,
            OUTER_BARRIER,
            eSetBits,
            NULL);
        lastStableOuterTs = currentTs;
    }
    else
    {
        ets_printf("DEBOUNCE %s\n", OUTER);
    }
}

void IRAM_ATTR innerIsr(void)
{
    uint64_t currentTs = esp_timer_get_time();
    if (currentTs - lastStableInnerTs > DEBOUNCE_TIME_IN_MICROSECONDS && !lockInnerTask)
    {
        xTaskNotifyFromISR(
            taskHandle2,
            INNER_BARRIER,
            eSetBits,
            NULL);
        lastStableInnerTs = currentTs;
    }
    else
    {
        ets_printf("DEBOUNCE %s\n", INNER);
    }
}

void app_main(void)
{
    esp_log_level_set(OUTER, ESP_LOG_INFO);
    esp_log_level_set(INNER, ESP_LOG_INFO);


    /* HW Setup */

    ESP_ERROR_CHECK(gpio_set_direction(OUTER_BARRIER_GPIO, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_direction(INNER_BARRIER_GPIO, GPIO_MODE_INPUT));

    gpio_pullup_en(OUTER_BARRIER_GPIO);
    gpio_pullup_en(INNER_BARRIER_GPIO);

    gpio_pulldown_dis(INNER_BARRIER_GPIO);
    gpio_pulldown_dis(OUTER_BARRIER_GPIO);

    /* Interrupts */

    gpio_install_isr_service(0);
    gpio_set_intr_type(OUTER_BARRIER_GPIO, GPIO_INTR_NEGEDGE);
    gpio_set_intr_type(INNER_BARRIER_GPIO, GPIO_INTR_NEGEDGE);

    /* Tasks */

    xTaskCreatePinnedToCore(
        outerTask,
        "Outer Task",
        4096,
        NULL,
        1,
        &taskHandle1,
        0);
    xTaskCreatePinnedToCore(
        innerTask,
        "Inner Task",
        4096,
        NULL,
        1,
        &taskHandle2,
        1);

    gpio_isr_handler_add(OUTER_BARRIER_GPIO, outerIsr, NULL);
    gpio_isr_handler_add(INNER_BARRIER_GPIO, innerIsr, NULL);

    /* Display */

    initDisplay();
    showRoomState();
}
