#ifndef MAIN_H
#define MAIN_H

#define INSTITUTE


//#define WITH_DISPLAY 1
#define SEND_DELAY 10 //Seconds

#ifdef INSTITUTE
    #define WIFI_SSID "CAPS-Seminar-Room"
    #define WIFI_PASS "caps-schulz-seminar-room-wifi"
    #define SNTP_SERVER "ntp1.in.tum.de"
    //#define SNTP_SERVER "pool.ntp.org"
#endif


//IoT Platform information
#define USER_NAME "group10" 
#define USERID 12  //to be found in the platform 
#define DEVICEID 5  //to be found in the platform
#define SENSOR_NAME "ourSensor" 
#define SENSOR_PREDICTION "prediction"
#define TOPIC "12_5" //userID_deviceID
#define JWT_TOKEN "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE2NTMzMDUyNDksImlzcyI6ImlvdHBsYXRmb3JtIiwic3ViIjoiMTJfNSJ9.Rz3JTZ6P8GatQpLWLLFLzAPaxl8j_3Svv2segJYBUHZA0Yy88GbqToQDd8i_TPagLr9fPQRJlCeDOoRi5yU4e8AG-H0s5_4SXKV0JDxi4rtzRV0l96p1xGpY8TmWFWmxDEYGmh2DRd_n85qIJtjHFjjDpffk5GGfjdKa2Da0Cz64QbsYPNhI_85cLw1FGY10UYH5TF0GiuW3xDqC_jhfoacCgUMVJFCdOdvEA4XMi0Uo-N5HdAFjBe1i7SqKK9Oq3PZhupzNnOIg-ecbUdH6SMO6YRkuOa1_k_u6rmxnMDAT4BZe_6c7vWGyqvlH6PN-FRmW2buWqRsOJDwp_Mw1CDcnqAgMLsg7iBldKz4aPb1jpSKNiaHGipo-HI59Pj6c8GvlRhe4KPBzMvXXxRpFCpGQhjZBzt92ZsypjVomVTJzbSdqnnagSQOaJeJxS9voUx5OKKsGGNb9nEIIR2RwsdZpyVeF4dqZ8RBOu8BnVq5TDWPNPzkyIAfilVEUdmsM6YfnUDnaq2zwwRqHUL9-mGSeF4XgkobZul3EjmoP3XI-PvmI-lOxIQ8WjnT8a_pUPeI1Y44-eeSssbDgwtu8Xv5YoyqUoUkDrC2scvhdPldOW7AI6Y4I-CeC4qa2q6uoNqePyUAAckJTXyqKHwBQJIYJ7an_rAJ1Rxn1QTRpWUE"
#define MQTT_SERVER "138.246.236.107"



#endif
