/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h" //These next three lines generate delays, create tasks and event groups
#include "freertos/task.h" 
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h" //enables wifi connectivity
#include "esp_event.h" //monitors wifi events
#include "esp_log.h" //logging library
#include "nvs_flash.h"
#include "esp_http_client.h" //http library

#include "lwip/err.h"
#include "lwip/sys.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID "Captains Quarters" //"TAMU_IoT"      //CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS "Gxd!TNM#Zys21" //""               //CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY //max retries to connect
//defines different security settings
#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH 
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK 
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0 //bit for successful wifi connection to access point
#define WIFI_FAIL_BIT      BIT1 //bit for failed wifi connection to access point

/*This code uses Information Logging. Log function takes in two arguements.
First arguement is the tag and second is a formatted string. So this global
global variable will be useful while calling the ESP_LOGI() functions*/
static const char *TAG = "wifi station"; // "wifi station" is the tag used while logging

static const char *TAG2 = "http"; // "http" is the tag used while logging HTTP

static int s_retry_num = 0;

//event_handler() function handles wifi events. Acts as callback function when a wifi or ip event occurs
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    //when wifi event WIFI_EVENT and WIFI_EVENT_STA_START occur...
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect(); //attempts to connect esp32 with access point
    //If esp32 unable to connect to access point then...
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        //loop to connect esp32 again until its connected to ap
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect(); //attempts to connect esp32 with access point
            s_retry_num++; //increases retry variable
            ESP_LOGI(TAG, "retry to connect to the AP"); //logs failed connection attempt
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT); //sets WIFI_FAIL_BIT
        }
        ESP_LOGI(TAG,"connect to the AP fail"); //logs failed connection
    //IP event occurs when esp32 board connects with our access point and router assigned an IP address to it
    //IP_EVENT_STA_GOT_IP gets ip
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip)); //logs ip address
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT); //sets WIFI_CONNECTED_BIT
    }
}

//initializes wifi in station mode for ESP32
void wifi_init_sta(void)
{
    //creates a FreeRTOS event group to monitor the connection, returns a handle to the event group
    s_wifi_event_group = xEventGroupCreate(); 

    //ESP_ERROR_CHECK() used to check if error occurs, if inside function doesn't return ESP_OK, logs error message 
    //esp_netif_init() creates IwIP task, better known as TCP/IP. 
    //IwIP is a TCP/IP library stack provided by ESP-IDF used to perform protocols such as TCP, UDP, DHCP, etc.
    ESP_ERROR_CHECK(esp_netif_init()); // 

    //create default event loop that enables system events to be sent to event task
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    //initializes wifi default for station mode
    //this API used to initialize and register event handlers for default interface (station in our case)
    //creates network interface instance binding wifi driver and tcp/ip stack
    //when station is in process of connecting to an access point, various processes get handling through this function
    esp_netif_create_default_wifi_sta();

    //initialized wifi configuration structure
    //wifi_init_config is a structure denoting the wifi stack configuration parameters passed into esp_wifi_init()
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    //intialize wifi allocate resource for wifi driver. Responsible for initiating the wifi task
    //takes in pointer to already initialized wifi configuration structure, so initialization of configuration is at default values
    ESP_ERROR_CHECK(esp_wifi_init(&cfg)); 

    esp_event_handler_instance_t instance_any_id; //creates event handler for wifi events
    esp_event_handler_instance_t instance_got_ip; //creates event handler for when station obtains ip
    //two registers for the event handler instances. This way application task can register for a callback that listens for events (Wifi or TCP/IP)
    //Events are ESP_EVENT_ANY_ID and IP_EVENT_STA_GOT_IP 
    //listens to all wifi events and ip events when the station gets its ip address
    //hence, the callback event_handler function will be called if any of these two events occur
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id)); //
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    //setting up SSID and password of network we want to connect to as well as assign security setting
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) ); //set wifi mode as station
    //assign our networks SSID and password. This way wifi driver gets configured with the access point network credentials and wifi mode
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() ); //start wifi connection at our assigned configuration

    ESP_LOGI(TAG, "wifi_init_sta finished."); //announces we are done with process of initializing the wifi in station mode

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    //this is a a blocked state, so this thread is waiting until it has access to an object
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", //if esp32 connects to access point, prints it got connected to specific ssid and password
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", //if esp32 doesnt connect, prints opposite
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

//handles events that occur during the http requests
//is a callback function that is passed to the esp_http_client_config_t structure
//when an event occurs this function is called with information about it
esp_err_t _http_event_handle(esp_http_client_event_t *evt) {

    static char *output_buffer;  //output buffer to store get request response 
    static int output_len;       //output length in bytes

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR: //logs an error event
            ESP_LOGD(TAG2, "HTTP_EVENT_ERROR"); 
            break;
        case HTTP_EVENT_ON_CONNECTED: //logs a connection event
            ESP_LOGD(TAG2, "HTTP_EVENT_ON_CONNECTED"); 
            break;
        case HTTP_EVENT_HEADER_SENT: //logs a header sent event
            ESP_LOGD(TAG2, "HTTP_EVENT_HEADER_SENT"); 
            break;
        case HTTP_EVENT_ON_HEADER: //logs the recieved headers
            ESP_LOGD(TAG2, "HTTP_EVENT_ON_HEADER"); 
            printf("%.*s", evt->data_len, (char*)evt->data); //prints the recieved headers
            break;
        case HTTP_EVENT_ON_DATA: //Logs and prints data received from the server
            ESP_LOGD(TAG2, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) { 
                //if the response is not chunked it prints the data
                printf("%.*s", evt->data_len, (char*)evt->data);
            
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

            }
            break;
        case HTTP_EVENT_ON_FINISH: //Logs a request finish event
            ESP_LOGD(TAG2, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED: //Logs a disconnection event
            ESP_LOGD(TAG2, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

//performs a http GET request
void http_request_task(char* url) {

    //configures the http client
    esp_http_client_config_t config = {
        .url = "http://dataservice.accuweather.com/forecasts/v1/daily/5day/346069?apikey=pHat28oGnPiyQdCaChTEfuPZkXuzzVff&language=en-us&details=true&metric=true",  //url,
        .event_handler = _http_event_handle,
    };
    //initializes the http client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    //performs the http request
    esp_err_t err = esp_http_client_perform(client);
    //checks if the request was successful
    if (err == ESP_OK) {
        //logs the status code and the content length
        ESP_LOGI(TAG2, "HTTP GET Status = %i, content_length = %lli",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG2, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    //closes the connection and frees all memory allocated to http client instance
    esp_http_client_cleanup(client);
    //deletes the task
    vTaskDelete(NULL);
}

void app_main(void)
{
    //Initialize NVS, flash memory
    esp_err_t ret = nvs_flash_init();
    //check if NVS partition doesnt contain any empty pages or if it contains data in a format different from current version of code
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase()); //erase the NVS flash
      ret = nvs_flash_init(); //initialize NVS
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA"); //prints 
    wifi_init_sta(); //initialize wifi for station mode
    
    //url for the http get request
    char url[] = "http://dataservice.accuweather.com/forecasts/v1/daily/5day/346069?apikey=pHat28oGnPiyQdCaChTEfuPZkXuzzVff&language=en-us&details=true&metric=true";
    //
    xTaskCreate(http_request_task, url, 8192, NULL, 5, NULL);
}
