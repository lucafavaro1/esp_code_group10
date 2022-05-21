#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ssd1306.h"
//#include "time.h"

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

// void printLocalTime()
// {
//   struct tm timeinfo;
//   if(!getLocalTime(&timeinfo)){
//     Serial.println("Failed to obtain time");
//     return;
//   }
//   Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
// }

/**
 * This function updates the counter on the oled display
 * Count is global variable that is updated in outerTask and innerTask functions
 */

void showRoomState(void)
{
    char buffer[1];
    ssd1306_clearScreen();
    itoa((int)count, buffer, 10); // Convert int to char*
    ssd1306_printFixedN(96, 0, "G-10", STYLE_NORMAL, 0.5);
    ssd1306_printFixedN(0, 0, "Count:", STYLE_NORMAL, 1);
    ssd1306_printFixedN(0, 16, buffer, STYLE_NORMAL, 1);
}

/**
 * Logic behind the counting algorithm for the outer barrier task
 * An array of 8 elements is saved to store the status of the barriers in following moments
 * the 8 elements are logically grouped in couples where the first one represents the outer barrier status and the second one the inner barrier status
 * When the barrier is broken the status is 1, otherwise 0.
 * 
 * Corner cases included: break first barrier, then decide not to enter/exit
 */

void outerTask(void)
{
    uint32_t barrier = 0;
    uint64_t wakeTime = 0;
    bool oldStateOuter, oldStateInner;
    bool waitingForRelease = false;
    bool stateTimeline[8]; // first outer, then inner

    // "barrier" is the variable that determines whether the task should sleep or just got triggered
    while (1)
    {
        if (barrier == 0)
        {
            // if barrier = 0 means that the timeout got triggered or a sequence concluded
            ets_printf("%s task sleeping..\n", OUTER);
            lockOuterTask = false;
            waitingForRelease = false;
            xTaskNotifyWait(
                ULONG_MAX,
                ULONG_MAX,
                &barrier,
                portMAX_DELAY);
            // here the task gets triggered, barrier = 1 is read, wakeup time is saved
            lockOuterTask = true;
            wakeTime = esp_timer_get_time();
            ets_printf("%s barrier interrupted with value %d\n", OUTER, barrier);
        }
        else
        {
            // sense the initial state (that should be 1 0)
            stateTimeline[0] = gpio_get_level(OUTER_BARRIER_GPIO);
            stateTimeline[1] = gpio_get_level(INNER_BARRIER_GPIO);
            // case in which is triggered by the movement in the opposite side, so the first measurement is [1,1] instead of [1,0]
            // therefore skip and go back sleeping
            if (stateTimeline[0] == 1 && stateTimeline[1] == 1)
            {
                ets_printf("\n OUTER BEGIN 1:1 CASE \n");
                barrier = 0;
            }

            if (barrier != 0)
            {
                // store the initial state in the first two elements of the vector
                oldStateOuter = stateTimeline[0];
                oldStateInner = stateTimeline[1];
                int i = 2;
                // if the timeout is not reached and the array is still not full (still not 4 status updates)
                while (esp_timer_get_time() - wakeTime < TASK_WAIT_TIME_IN_MICROSECONDS && i < 8)
                {
                    // sense the state of the barriers
                    int currentOuter = gpio_get_level(OUTER_BARRIER_GPIO);
                    int currentInner = gpio_get_level(INNER_BARRIER_GPIO);
                    // in case you sense both 0 0 but it's not the last iteration of the cycle, then skip the case (something went bad)
                    if (currentInner == currentOuter && currentInner == 0 && i != 6)
                    {
                        ets_printf("\n GOTO EXIT CYCLE \n");
                        goto endCycle;
                    }
                    // if the state changed from the previous than save the new state in the vector
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
                // finally when the array is full (4 states, each with 2 bits) print the state
                ets_printf("OUTER: Before printing the array (right is 1 0 1 1 0 1 0 0) \n");

                for (int j = 0; j < 8; j = j + 2)
                {
                    ets_printf("[%d, %d]", stateTimeline[j], stateTimeline[j + 1]);
                }
                // and check if the state log is corrent, the only valid sequence for entering is <1,0> <1,1> <0,1>, <0,0>
                bool rightTimeline[8] = {1, 0, 1, 1, 0, 1, 0, 0};
                int counterEquals = 0;
                // compare the sequence
                for (int j = 0; j < 8; j++)
                {
                    if (stateTimeline[j] == rightTimeline[j])
                        counterEquals++;
                }
                // in case the sequence is correct, increment the counter
                if (counterEquals == 8)
                {
                    count++;
                    showRoomState();
                }

            endCycle:
                // set barrier to 0 so in the next iteration of the cycle the task will sleep
                barrier = 0;
            }
            vTaskDelay(10); // Wait 10 ticks
        }
    }
}

/**
 * Logic behind the counting algorithm for the inner barrier task
 * An array of 8 elements is saved to store the status of the barriers in following moments
 * the 8 elements are logically grouped in couples where the first one represents the outer barrier status and the second one the inner barrier status
 * When the barrier is broken the status is 1, otherwise 0.
 * 
 * Corner cases included: break first barrier, then decide not to enter/exit
 */

void innerTask(void)
{
    uint32_t barrier = 0;
    uint64_t wakeTime = 0;
    bool oldStateOuter, oldStateInner;
    bool waitingForRelease = false;
    bool stateTimeline[8]; // first outer, then inner

    // "barrier" is the variable that determines whether the task should sleep or just got triggered
    while (1)
    {
        if (barrier == 0)
        {
            // if barrier = 0 means that the timeout got triggered or a sequence concluded
            ets_printf("%s task sleeping..\n", INNER);
            lockInnerTask = false;
            waitingForRelease = false;
            xTaskNotifyWait(
                ULONG_MAX,
                ULONG_MAX,
                &barrier,
                portMAX_DELAY);
            // here the task gets triggered, barrier = 1 is read, wakeup time is saved
            lockInnerTask = true;
            wakeTime = esp_timer_get_time();
            ets_printf("%s barrier interrupted with value %d\n", INNER, barrier);
        }
        else
        {
            // sense the initial state (that should be 0 1)
            stateTimeline[0] = gpio_get_level(OUTER_BARRIER_GPIO);
            stateTimeline[1] = gpio_get_level(INNER_BARRIER_GPIO);
            // case in which is triggered by the opposite size, so the first measurement is [1,1] instead of [0,1]
            // because you are moving in
            if (stateTimeline[0] == 1 && stateTimeline[1] == 1)
            {
                barrier = 0;
                ets_printf("\n INNER BEGIN 1:1 CASE \n");
            }

            if (barrier != 0)
            {
                // store the initial state in the first two elements of the vector    
                oldStateOuter = stateTimeline[0];
                oldStateInner = stateTimeline[1];
                int i = 2;
                // if the timeout is not reached and the array is still not full (still not 4 status updates)
                while (esp_timer_get_time() - wakeTime < TASK_WAIT_TIME_IN_MICROSECONDS && i < 8)
                {
                    // sense the state of the barriers    
                    int currentOuter = gpio_get_level(OUTER_BARRIER_GPIO);
                    int currentInner = gpio_get_level(INNER_BARRIER_GPIO);
                    // in case you sense both 0 0 but it's not the last iteration of the cycle, then skip the case (something went bad)
                    if (currentInner == currentOuter && currentInner == 0 && i != 6)
                    {
                        ets_printf("\n GOTO EXIT CYCLE \n");
                        goto endCycle;
                    }
                    // if the state changed from the previous than save the new state in the vector
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
                // finally when the array is full (4 states, each with 2 bits) print the state
                ets_printf("INNER: Before printing the array (right is 0 1 1 1 1 0 0 0) \n");

                for (int j = 0; j < 8; j = j + 2)
                {
                    ets_printf("[%d, %d]", stateTimeline[j], stateTimeline[j + 1]);
                }
                // and check if the state log is corrent, the only valid sequence for entering is <0,1> <1,1> <1,0>, <0,0>

                bool rightTimeline[8] = {0, 1, 1, 1, 1, 0, 0, 0};
                int counterEquals = 0;
                // compare the sequence
                for (int j = 0; j < 8; j++)
                {
                    if (stateTimeline[j] == rightTimeline[j])
                        counterEquals++;
                }
                // in case the sequence is correct, increment the counter
                if (counterEquals == 8)
                {
                    if (count > 0)
                        count--;
                    showRoomState();
                }
            endCycle:
            // set barrier to 0 so in the next iteration of the cycle the task will sleep
                barrier = 0;
            }
            vTaskDelay(10); // Wait 10 ticks
        }
    }
}

// outer barrier interrupt
void IRAM_ATTR outerIsr(void)
{
    uint64_t currentTs = esp_timer_get_time();
    if (currentTs - lastStableOuterTs > DEBOUNCE_TIME_IN_MICROSECONDS && !lockOuterTask)
    {
        // the task is notified only if it's not in the "debounce" period or already running(lockOuterTask)
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

// inner barrier interrupt
void IRAM_ATTR innerIsr(void)
{
    uint64_t currentTs = esp_timer_get_time();
    if (currentTs - lastStableInnerTs > DEBOUNCE_TIME_IN_MICROSECONDS && !lockInnerTask)
    {
        // the task is notified only if it's not in the "debounce" period or already running(lockOuterTask)
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

    // pullup and pulldown are not working with our hardware :)
    gpio_pullup_en(OUTER_BARRIER_GPIO);
    gpio_pullup_en(INNER_BARRIER_GPIO);

    gpio_pulldown_dis(INNER_BARRIER_GPIO);
    gpio_pulldown_dis(OUTER_BARRIER_GPIO);

    /* Interrupts */

    gpio_install_isr_service(0);
    gpio_set_intr_type(OUTER_BARRIER_GPIO, GPIO_INTR_NEGEDGE); // those are not working either, they act as anyedge
    gpio_set_intr_type(INNER_BARRIER_GPIO, GPIO_INTR_NEGEDGE);

    // printLocalTime(); skip this for now

    /* Tasks */

    // one task per core is assigned
    // each task manages the interrupt from one barrier
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
