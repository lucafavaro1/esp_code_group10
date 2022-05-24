/* ADC1 Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

#define DEFAULT_VREF    1100        //Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES   64          //Multisampling

static esp_adc_cal_characteristics_t *adc_chars;
static const adc_channel_t channel = ADC_CHANNEL_5;     //GPIO34 if ADC1, GPIO14 if ADC2
static const adc_channel_t channel2 = ADC_CHANNEL_6;     //GPIO34 if ADC1, GPIO14 if ADC2

void app_main()
{
    //Continuously sample ADC1
    while (1) {
        //Convert adc_reading to voltage in mV
        printf("GPIO33: %4d\tGPIO34: %4d\n", adc1_get_raw((adc1_channel_t)channel), adc1_get_raw((adc1_channel_t)channel2));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


