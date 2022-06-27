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
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_event_loop.h"
#include "ssd1306.h"
#include "nvs_flash.h"
#include "wifi.h"
#include "timeMgmt.h"
#include "mqtt.h"
#include "main.h"
#include "cJSON.h"

#define OUTER_BARRIER_ADC ADC_CHANNEL_5
#define OUTER_BARRIER_GPIO CONFIG_LIGHT1_GPIO

#define INNER_BARRIER_ADC ADC_CHANNEL_6
#define INNER_BARRIER_GPIO CONFIG_LIGHT2_GPIO

#define POSEDGE_THRESHOLD_RAW 0                  // Anything above 0 is a pos edge (entering movement)
#define NEGEDGE_THRESHOLD_RAW 2270               // Anything above 2270 is a neg edge (leaving movement)
#define DEBOUNCE_TIME_IN_MICROSECONDS 1000 * 100 // in miliseconds

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
volatile uint8_t __attribute__ ((section(".noinit"))) count;
volatile uint8_t __attribute__ ((section(".noinit"))) firstTime;
volatile uint8_t prediction = 0;
volatile uint8_t prediction_ts = 0;
volatile uint64_t lastStableOuterTs = 0;
volatile uint64_t lastStableInnerTs = 0;
static IRAM_ATTR TaskHandle_t outerBarrierTaskHandle;
static IRAM_ATTR TaskHandle_t innerBarrierTaskHandle;

void initDisplay()
{
    ssd1306_128x64_i2c_init();
    ssd1306_setFixedFont(ssd1306xled_font6x8);
    ssd1306_clearScreen();
}

void showRoomState(void)
{
    time_t rawtime;
    struct tm *timeinfo;
    char countString[50];
    char timeToPrint[50];
    char predictionString[50];

    ssd1306_clearScreen();
    ssd1306_printFixedN(0, 0, "G10", STYLE_NORMAL, 1);
    // get time locally, put in a prefixed data structure and get the hour and minute fields
    time(&rawtime);
    timeinfo = localtime(&rawtime);

    // convert types in string before printing !! DONT REMOVE, IT CAUSES MEMORY ERROR !!
    sprintf(countString, "%d", count);
    sprintf(predictionString, "%d", prediction);
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
    ssd1306_printFixedN(0, 32, countString, STYLE_NORMAL, 2); // counter
    ssd1306_printFixedN(96, 32, predictionString, STYLE_NORMAL, 2);         // prediction
}

// separate task for updating the display
void displayTask(void)
{
    while (1)
    {
        showRoomState();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            break;
    }
    return ESP_OK;
}

void getPrediction()
{
    char buffer[255] = {0};
    char *out;
    esp_http_client_config_t config = {
            .host = "34.159.167.5",
            .path = "/predict",
            .auth_type = HTTP_AUTH_TYPE_NONE,
            .user_data = &buffer,
            .event_handler = _http_event_handler
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK && esp_http_client_get_status_code(client) == 200) {
        int size = esp_http_client_get_content_length(client);
        cJSON *root = cJSON_Parse(buffer);
        if (root) {
            prediction = cJSON_GetObjectItem(root, "prediction")->valueint;
            cJSON_Delete(root);
        }
    } else {
        ets_printf("HTTP GET request failed: %s", esp_err_to_name(err));
    }
}

void sendToMqtt(char* username, char* sensorName, int count, int deviceId, unsigned long rawtime)
{
    char msg[256];
    int qos_test = 1;
    sprintf(msg, "{\"username\":\"%s\",\"%s\":%d,\"device_id\":\"%d\",\"timestamp\":%lu000}", USER_NAME, SENSOR_NAME, count, DEVICEID, rawtime);
    ESP_LOGI("MQTT_SEND", "Topic %s: %s\n", TOPIC, msg);
    int msg_id = esp_mqtt_client_publish(mqttClient, TOPIC, msg, strlen(msg), qos_test, 0);
    if (msg_id == -1)
    {
        ESP_LOGE(TAG, "msg_id returned by publish is -1!\n");
    }
}

// separate task to send the "count" value to the MQTT platform every 5 mins
void retrieveAndSend(void)
{
    time_t now = 0;

    while (1)
    {
        char msg[256];
        time_t rawtime;
        struct tm *timeinfo;

        time(&rawtime);
        timeinfo = localtime(&rawtime);

        // check if it's midnight, in that case reset counter if it's not 0
        if (timeinfo->tm_hour == 0 && (timeinfo->tm_min >= 0 || timeinfo->tm_min < 5))
            if (count > 0)
                count = 0;

        getPrediction();

        // send the sensor value to the mqtt platform
        sendToMqtt(USER_NAME, SENSOR_NAME, count, DEVICEID, rawtime);

        // send the prediction value to the mqtt platform
        sendToMqtt(USER_NAME, SENSOR_PREDICTION, prediction, DEVICEID, rawtime + 900); // Add 900 seconds to the ts

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
                ets_printf("Outer raw: %d \n", raw);
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
                ets_printf("Inner raw: %d \n", raw);
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
                // ets_printf("RESET INCREMENT\n");
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
            // ets_printf("DECREMENT TASK UL: %d\n", ulNotification);
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
                        ets_printf("--\n");
                    }
                    ulNotification = TASK_SLEEP;
                }
                else
                {
                    ulNotification = TASK_RESET;
                }

            case TASK_RESET:
                // ets_printf("RESET DECREMENT\n");
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
    uint64_t currentTs = esp_timer_get_time();
    if (currentTs - lastStableOuterTs > DEBOUNCE_TIME_IN_MICROSECONDS)
    {
        // ets_printf("COVERED OUTER\n");
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
    lastStableOuterTs = currentTs;
    }
}

// interrupt function, each time that the inner barrier is broken both tasks are resumed
void IRAM_ATTR innerBarrierIsr(void)
{
    uint64_t currentTs = esp_timer_get_time();
    if (currentTs - lastStableInnerTs > DEBOUNCE_TIME_IN_MICROSECONDS)
    {
        // ets_printf("COVERED INNER\n");
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
    lastStableInnerTs = currentTs;
    }
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

    if(firstTime != 42) {
        count = 0;
        firstTime = 42;
    }


    initWifi();
    initSNTP();
    initMQTT();

    /* HW Setup */

    ESP_ERROR_CHECK(gpio_set_direction(OUTER_BARRIER_GPIO, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_direction(INNER_BARRIER_GPIO, GPIO_MODE_INPUT));

    // pullup and pulldown are not working with our hardware :)
    // gpio_pullup_dis(OUTER_BARRIER_GPIO);
    // gpio_pullup_dis(INNER_BARRIER_GPIO);

    // gpio_pulldown_en(INNER_BARRIER_GPIO);
    // gpio_pulldown_en(OUTER_BARRIER_GPIO);

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
    xTaskCreate(retrieveAndSend, "Send Data Task", 4096, NULL, 3, NULL);
}
