#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include <driver/adc.h>
#include "sdkconfig.h"
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "ssd1306.h"
#include "nvs_flash.h"
#include "wifi.h"
#include "timeMgmt.h"
#include "mqtt.h"
#include "main.h"

#define OUTER_BARRIER_ADC ADC_CHANNEL_4
#define OUTER_BARRIER_GPIO CONFIG_LIGHT1_GPIO

#define INNER_BARRIER_ADC ADC_CHANNEL_5
#define INNER_BARRIER_GPIO CONFIG_LIGHT2_GPIO

#define POSEDGE_THRESHOLD_RAW 0                  // Anything above 0 is a pos edge (entering movement)
#define NEGEDGE_THRESHOLD_RAW 2270               // Anything above 2270 is a neg edge (leaving movement)

// state machine states definition
#define TASK_SLEEP 0
#define OUTER_IN 1
#define OUTER_OUT 2
#define INNER_IN 3
#define INNER_OUT 4
#define TASK_RESET 5
#define OUTER 6
#define INNER 7

static const char *TAG = "BLINK";
volatile uint8_t count = 0;
volatile uint64_t lastStableOuterTs = 0;
volatile uint64_t lastStableInnerTs = 0;
static IRAM_ATTR TaskHandle_t outerBarrierTaskHandle;
static IRAM_ATTR TaskHandle_t innerBarrierTaskHandle;

void initDisplay()
{
    ssd1306_128x32_i2c_init();
    ssd1306_setFixedFont(ssd1306xled_font6x8);
    ssd1306_clearScreen();
}

void showRoomState(void)
{
    time_t rawtime;
    struct tm *timeinfo;
    char countString[50];
    char timeToPrint[50];

    ssd1306_clearScreen();
    ssd1306_printFixedN(0, 0, "G10", STYLE_NORMAL, 1);
    // get time locally, put in a prefixed data structure and get the hour and minute fields
    time(&rawtime);
    timeinfo = localtime(&rawtime);

    // convert types in string before printing !! DONT REMOVE, IT CAUSES MEMORY ERROR !!
    sprintf(countString, "%d", count);
    if (timeinfo->tm_hour < 10 && timeinfo->tm_min < 10)
        sprintf(timeToPrint, "0%d:0%d", timeinfo->tm_hour, timeinfo->tm_min);
    else if (timeinfo->tm_hour < 10)    
        sprintf(timeToPrint, "0%d:%d", timeinfo->tm_hour, timeinfo->tm_min);
    else if (timeinfo->tm_min < 10)
        sprintf(timeToPrint, "%d:0%d", timeinfo->tm_hour, timeinfo->tm_min);
    else
        sprintf(timeToPrint, "%d:%d", timeinfo->tm_hour, timeinfo->tm_min);

    // print on the screen HH:MM
    ssd1306_printFixedN(64, 0, timeToPrint, STYLE_NORMAL, 1);
    ssd1306_printFixedN(16, 16, countString, STYLE_NORMAL, 1); // counter
    ssd1306_printFixedN(96, 16, "0", STYLE_NORMAL, 1);       // prediction
}

// separate task for updating the display
void displayTask(void)
{
    while (1)
    {
        showRoomState();
        vTaskDelay(100);
    }
}

// separate task to send the "count" value to the MQTT platform every 5 mins
void sendData(void)
{
    time_t now = 0;

    while (1)
    {
        char msg[256];
        int qos_test = 1;
        time_t rawtime;
        struct tm *timeinfo;

        time(&rawtime);
        timeinfo = localtime(&rawtime);

        // check if it's midnight, in that case reset counter if it's not 0
        if (timeinfo->tm_hour == 0 && (timeinfo->tm_min >= 0 || timeinfo->tm_min < 5))
            if (count > 0)
                count = 0;

        sprintf(msg, "{\"username\":\"%s\",\"%s\":%d,\"device_id\":\"%d\",\"timestamp\":%lu000}", USER_NAME, SENSOR_NAME, count, DEVICEID, rawtime);
        ESP_LOGI("MQTT_SEND", "Topic %s: %s\n", TOPIC, msg);
        int msg_id = esp_mqtt_client_publish(mqttClient, TOPIC, msg, strlen(msg), qos_test, 0);
        if (msg_id == -1)
        {
            ESP_LOGE(TAG, "msg_id returned by publish is -1!\n");
        }

        vTaskDelay(pdMS_TO_TICKS(300 * 1000)); // wait for 5 mins = 300 sec = 300*1000 ms
    }
}

// state machine implementation for the task that should increment the counter
void incrementTask(void *params)
{

    uint32_t ulNotification = 0;
    bool states[5];
    uint32_t raw;
    for (int i = 0; i < 5; i++)
    {
        states[i] = false;
    }

    while (1)
    {   
        // after each step further in the state machine, back to sleep
        if (ulNotification == TASK_SLEEP)
        {
            xTaskNotifyWait(
                ULONG_MAX,
                ULONG_MAX,
                &ulNotification,
                portMAX_DELAY);
        }
        else
        {
            switch (ulNotification)
            {

            // step 0: check if it is a rising or falling edge on the outer barrier
            case OUTER:
                vTaskDelay(1);
                raw = adc1_get_raw(OUTER_BARRIER_ADC);
                // if it is a falling edge, jump to step 3 
                if (raw > NEGEDGE_THRESHOLD_RAW)
                {
                    ulNotification = OUTER_OUT;
                }
                else
                // if it is a rising edge, jump to step 1 (potentially entering)
                {
                    ulNotification = OUTER_IN;
                }
                break;

            // step 0: check if it is a rising or falling edge on the inner barrier
            case INNER:
                vTaskDelay(1);
                raw = adc1_get_raw(INNER_BARRIER_ADC);
                // if it is a falling edge, jump to step 4 
                if (raw > NEGEDGE_THRESHOLD_RAW)
                {
                    ulNotification = INNER_OUT;
                }
                else
                // if it is a rising edge, jump to step 2

                {
                    ulNotification = INNER_IN;
                }
                break;

            // step 1: break the outer barrier
            case OUTER_IN:
                states[OUTER_IN] = true;
                ulNotification = TASK_SLEEP;
                break;

            // step 3: release the outer barrier
            case OUTER_OUT: 
                if (states[OUTER_IN] && states[INNER_IN])
                {
                    states[OUTER_OUT] = true;
                    ulNotification = TASK_SLEEP;
                }
                else
                {
                    ulNotification = TASK_RESET;
                }
                break;

            // step 2: break the inner barrier
            case INNER_IN:
                if (states[OUTER_IN])
                {
                    states[INNER_IN] = true;
                    ulNotification = TASK_SLEEP;
                }
                else
                {
                    ulNotification = TASK_RESET;
                }
                break;

            // step 4: release inner barrier 
            case INNER_OUT:
                if (states[OUTER_IN] && states[INNER_IN] && states[OUTER_OUT])
                {
                    count++;
                    ets_printf("++\n");
                    ulNotification = TASK_SLEEP;
                }
                else
                {
                    ulNotification = TASK_RESET;
                }

            case TASK_RESET:
                //ets_printf("RESET INCREMENT\n");
                for (int i = 0; i < 5; i++)
                {
                    states[i] = false;
                }
                ulNotification = TASK_SLEEP;
                break;

            default:
                break;
            }
        }
    }
}

// state machine implementation for the task that should increment the counter
void decrementTask(void *params)
{

    uint32_t ulNotification = 0;
    bool states[5];
    uint32_t raw;
    for (int i = 0; i < 5; i++)
    {
        states[i] = false;
    }

    while (1)
    {
        // after each step further in the state machine, back to sleep
        if (ulNotification == TASK_SLEEP)
        {
            xTaskNotifyWait(
                ULONG_MAX,
                ULONG_MAX,
                &ulNotification,
                portMAX_DELAY);
        }
        else
        {
            switch (ulNotification)
            {
            // step 0: check if it is a rising or falling edge on the outer barrier
            case OUTER:
                vTaskDelay(1);
                raw = adc1_get_raw(OUTER_BARRIER_ADC);
                ets_printf("Outer raw: %d \n", raw);
                // if it is a falling edge, jump to step 4
                if (raw > NEGEDGE_THRESHOLD_RAW)
                {
                    ulNotification = OUTER_OUT;
                }
                else
                // if it is a rising edge, jump to step 2
                {
                    ulNotification = OUTER_IN;
                }
                break;

            // step 0: check if it is a rising or falling edge on the inner barrier
            case INNER:
                vTaskDelay(1);
                raw = adc1_get_raw(INNER_BARRIER_ADC);
                ets_printf("Inner raw: %d \n", raw);
                // if it is a falling edge, jump to step 3
                if (raw > NEGEDGE_THRESHOLD_RAW)
                {
                    ulNotification = INNER_OUT;
                }
                else
                // if it is a rising edge, jump to step 1 
                {
                    ulNotification = INNER_IN;
                }
                break;

            // step 1: break the inner barrier
            case INNER_IN:
                states[INNER_IN] = true;
                ulNotification = TASK_SLEEP;
                break;

            // step 3: release inner barrier
            case INNER_OUT:
                if (states[INNER_IN] && states[OUTER_IN])
                {
                    states[INNER_OUT] = true;
                    ulNotification = TASK_SLEEP;
                }
                else
                {
                    ulNotification = TASK_RESET;
                }
                break;

            // step 2: break the outer barrier
            case OUTER_IN:
                if (states[INNER_IN])
                {
                    states[OUTER_IN] = true;
                    ulNotification = TASK_SLEEP;
                }
                else
                {
                    ulNotification = TASK_RESET;
                }
                break;

            // step 4: release outer barrier
            case OUTER_OUT:
                if (states[INNER_IN] && states[OUTER_IN] && states[INNER_OUT])
                {
                    if (count > 0)
                    {
                        count--;
                    }
                    ets_printf("--\n");
                    ulNotification = TASK_SLEEP;
                }
                else
                {
                    ulNotification = TASK_RESET;
                }

            case TASK_RESET:
                //ets_printf("RESET DECREMENT\n");
                for (int i = 0; i < 5; i++)
                {
                    states[i] = false;
                }
                ulNotification = TASK_SLEEP;
                break;

            default:
                break;
            }
        }
    }
}

// interrupt function, each time that the outer barrier is broken both tasks are resumed
void IRAM_ATTR outerBarrierIsr(void)
{
        xTaskNotifyFromISR(
            outerBarrierTaskHandle,
            OUTER,
            eSetBits,
            NULL);
        xTaskNotifyFromISR(
            innerBarrierTaskHandle,
            OUTER,
            eSetBits,
            NULL);
    
}

// interrupt function, each time that the inner barrier is broken both tasks are resumed
void IRAM_ATTR innerBarrierIsr(void)
{
        xTaskNotifyFromISR(
            outerBarrierTaskHandle,
            INNER,
            eSetBits,
            NULL);
        xTaskNotifyFromISR(
            innerBarrierTaskHandle,
            INNER,
            eSetBits,
            NULL);
}

void app_main(void)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);
    esp_log_level_set("*", ESP_LOG_ERROR);
    esp_log_level_set("MQTT", ESP_LOG_INFO);
    esp_log_level_set("MQTT_SEND", ESP_LOG_INFO);
    esp_log_level_set("PROGRESS", ESP_LOG_INFO);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Wifi - Time sync - IoT platform Setup */

    initWifi();
    initSNTP();
    initMQTT();

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
    gpio_set_intr_type(OUTER_BARRIER_GPIO, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add(OUTER_BARRIER_GPIO, outerBarrierIsr, NULL);
    gpio_set_intr_type(INNER_BARRIER_GPIO, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add(INNER_BARRIER_GPIO, innerBarrierIsr, NULL);

    /* Tasks */

    // one task per core is assigned
    // each task manages the interrupt from one barrier
    xTaskCreatePinnedToCore(
        incrementTask,
        "Increment Task",
        4096,
        NULL,
        1,
        &outerBarrierTaskHandle,
        0);
    xTaskCreatePinnedToCore(
        decrementTask,
        "Decrement Task",
        4096,
        NULL,
        1,
        &innerBarrierTaskHandle,
        1);

    initDisplay();

    // plus one task for the update of the display
    xTaskCreate(displayTask, "Display Task", 4096, NULL, 2, NULL);

    // plus one task to send data every 5 mins to MQTT
    xTaskCreate(sendData, "Send Data Task", 4096, NULL, 3, NULL);
}
