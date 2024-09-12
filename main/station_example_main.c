#include <string.h> // library for handling strings
//checking commit
#include <inttypes.h> // library for handling int
//These next three lines generate delays, create tasks and event groups
//freertos is used to easily design and develop a connected device and IoT applications
#include "freertos/FreeRTOS.h" //ESP-IDF's version of FREERTOS
//Allows for the structuring of our application as a set of independent tasks. A task is code that is schedulable
//RTOS scheduler decides which task is ran and only does one at a time. May stop, start and swap tasks. Each task has own stack
#include "freertos/task.h" 
//Event group library allows for the creation of a set of bits that can be used to track the state of the system
//An event group is a set of event bits. Individual event bits within an event group are referenced by a bit number
#include "freertos/event_groups.h"
//contains system apis for the ESP32 (Mac address, chip/ap/sdk versions, software reset, heap memory)
#include "esp_system.h"
#include "esp_wifi.h" //libary for enabling wifi connectivity
#include "esp_event.h" //libary for monitoring events
#include "esp_log.h" //library for logging
#include "nvs_flash.h" //library for non volatile storage
#include "esp_http_client.h" //library for http 

#include "lwip/err.h"
#include "lwip/sys.h"

#include "esp_crt_bundle.h"  
#include "esp_tls.h"

//#include "stdio.h"
//#include "stdlib.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
//#include "pretty_effect.h"

//alot of these libraries are not essential, they are remains of attempts at connecting the lcd to the mcu

//On the left side of display, pin2 is power(Vdd), pin 4 is RS(CS), pin5 is RW(SID), pin6 is E(SCLK), pin7 is SOD(DB0)
//On esp32 RS goes to pin 29(IO5), SCLK goes to pin30(IO18), SOD goes to pin31(IO19), RW goes to pin37(IO23)

#include "driver/uart.h"

#include "cJson.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID "TAMU_IoT" //"Captains Quarters" //CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS "" //"Gxd!TNM#Zys21"       //CONFIG_ESP_WIFI_PASSWORD
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
#define MAX_HTTP_OUTPUT_BUFFER 23000 // Adjust the buffer size as needed

// Structure to hold data during the HTTP request
typedef struct {
    char *data;
    size_t len;
} http_request_data_t;

static int s_retry_num = 0;

//event_handler() function handles wifi events. Acts as callback function when a wifi or ip event occurs
//when an event occurs this function is called with information about it
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
    //Creates a new RTOS event group, and returns a handle by which the newly created event group can be referenced
    s_wifi_event_group = xEventGroupCreate(); 

    //ESP_ERROR_CHECK() used to check if error occurs, if inside function doesn't return ESP_OK, logs error message  
    //IwIP is a TCP/IP library stack provided by ESP-IDF used to perform protocols such as TCP, UDP, DHCP, etc.
    //IP has core function of delivering packets of information from source device to target device
    //TCP handles packet ordering and error checking
    //ESP-NETIF serves as an intermediary between an IO driver and a network stack

    //esp_netif_init() creates IwIP task, better known as TCP/IP.
    ESP_ERROR_CHECK(esp_netif_init()); 

    //create default event loop that enables system events to be sent to event task
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //creates esp_netif object with default WiFi station config (station)

    //this API used to initialize and register event handlers for default interface (station in our case)
    //creates network interface instance binding wifi driver and tcp/ip stack
    //Wifi driver is software that helps user devices find and connect to wireless connections
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
        ESP_LOGI(TAG, "Connected to ap SSID:%s password:%s", //if esp32 connects to access point, prints it got connected to specific ssid and password
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", //if esp32 doesnt connect, prints opposite
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}


void process_json(cJSON *item) {
    if (cJSON_IsObject(item)) {
        cJSON *value = cJSON_GetObjectItemCaseSensitive(item, "RelativeHumidity");
        if (value != NULL) {
            if (cJSON_IsString(value)) {
                ESP_LOGI(TAG2, "Value: %s", value->valuestring);
            }
        }
        cJSON *child = NULL;
        cJSON_ArrayForEach(child, item) {
            process_json(child);
        }
    } else if (cJSON_IsArray(item)) {
        cJSON *element = NULL;
        cJSON_ArrayForEach(element, item) {
            process_json(element);
        }
    }
}



//handles events that occur during the http requests
//a handle is a an abstract reference to a resource. Could be a pointer, reference to an object, index value to an array, etc.
//is a callback function that is passed to the esp_http_client_config_t structure
//when an event occurs this function is called with information about it
esp_err_t _http_event_handle(esp_http_client_event_t *evt) 
{
    http_request_data_t *request_data = (http_request_data_t *)evt->user_data;

    //When you say char * str1 in C, you are allocating a pointer in the memory. 
    //When you write str1 = "Hello";, you are creating a string literal in memory and making the pointer point to it. 
    //When you create another string literal "new string" and assign it to str1, all you are doing is changing where the pointer points.
    //static char *output_buffer;  //output buffer to store get request response 
    //static int output_len;       //output length in bytes

    //When an object is accessed through a pointer, the arrow operator -> is used to access its members
    //switch for different http events
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR: //logs an error event
            ESP_LOGI(TAG2, "HTTP_EVENT_ERROR"); 
            break;
        case HTTP_EVENT_ON_CONNECTED: //logs a connection event
            ESP_LOGI(TAG2, "HTTP_EVENT_ON_CONNECTED"); 
            break;
        case HTTP_EVENT_HEADER_SENT: //logs a header sent event
            ESP_LOGI(TAG2, "HTTP_EVENT_HEADER_SENT"); 
            break;
        case HTTP_EVENT_ON_HEADER: //logs the recieved headers
            ESP_LOGI(TAG2, "HTTP_EVENT_ON_HEADER"); 
            printf("%.*s", evt->data_len, (char*)evt->data); //prints the recieved headers
            break;
        case HTTP_EVENT_ON_DATA: //Logs and prints data received from the server
            ESP_LOGI(TAG2, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Write data to buffer
                //memcpy() copies block of memory from one location to another
                //void *memcpy(void *to, const void *from, size_t numBytes);
                //to where copied data is stored, from where data is copied from, numBytes number of bytes copied
                memcpy(request_data->data + request_data->len, evt->data, evt->data_len);
                request_data->len += evt->data_len;
                printf("%.*s", evt->data_len, (char*)evt->data);

                /*
                cJSON *root = cJSON_Parse(evt->data);
                if (root != NULL) {
                    process_json(root);
                    cJSON_Delete(root);
                } else {
                    ESP_LOGE(TAG2, "Error parsing JSON: %s", cJSON_GetErrorPtr());
                }
                */
            }

            
            /*if (!esp_http_client_is_chunked_response(evt->client)) { 
                //if the response is not chunked it prints the data
                printf("%.*s", evt->data_len, (char*)evt->data);
            }
            */
            /*
            if (!esp_http_client_is_chunked_response(evt->client)) {

                //if the response is not chunked it prints the data
                printf("%.*s", evt->data_len, (char*)evt->data);

                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    //memcpy() copies block of memory from one location to another
                    //void *memcpy(void *to, const void *from, size_t numBytes);
                    //to where copied data is stored, from where data is copied from, numBytes number of bytes copied
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    //if user_data buffer is not configured, allocate memory for output_buffer
                    if (output_buffer == NULL) {
                        //dynamically allocates a large block of memory with a specified size
                        //returns a pointer of type void which can be cast into a pointer of any form
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
            */

            break;
        case HTTP_EVENT_ON_FINISH: //Logs a request finish event
            ESP_LOGI(TAG2, "HTTP_EVENT_ON_FINISH");
            break;
            
            /*
            //frees memory allocated for output buffer 
            if (output_buffer != NULL) {
                //response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                ESP_LOG_BUFFER_HEX(TAG2, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            */
            
            break;
        case HTTP_EVENT_DISCONNECTED: //Logs a disconnection event
            ESP_LOGI(TAG2, "HTTP_EVENT_DISCONNECTED");
            
            /*
            //gets and logs last ESP error code and mbedtls failure
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
        
            //frees memory allocated for output_buffer
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            */

            break;
        default:
            break;
    }
    return ESP_OK;
}

//task handler that performs our http GET request task (takes pointer to url)
static void http_request_task(void *) { //(char* url)

    //struct that configures the http client instance
    //enables event handling
    esp_http_client_config_t config = {
        .url = "http://dataservice.accuweather.com/forecasts/v1/daily/5day/346069?apikey=pHat28oGnPiyQdCaChTEfuPZkXuzzVff&language=en-us&details=true&metric=true",
        //.crt_bundle_attach = esp_crt_bundle_attach,
        //.user_data = responseBuffer,
        .event_handler = _http_event_handle,
    };

    // struct 
    http_request_data_t request_data = {
        //malloc assigns a certain amount of memory for an array to be created. Returns a pointer to that space
        //the ()before malloc defines the data type of the pointer being created and the input to malloc is the size in bytes that is to be allocated
        .data = malloc(MAX_HTTP_OUTPUT_BUFFER), //add (char*) before malloc?
        .len = 0
    };

    //Starts an http session using the information from config. Returns a esp_http_client_handle_t used as input to other function
    //& get the address of the variable while * sets something as a pointer
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_user_data(client, &request_data); // Set user data to access in event handler
    //performs the http get request
    //opens connection, collects data, closes connection. All related events invoked through event handler
    esp_err_t err = esp_http_client_perform(client);
    //checks if the request was successful
    if (err == ESP_OK) {
        if (esp_http_client_get_status_code(client) == 200) {
            //logs the status code and the content length
            ESP_LOGI(TAG2, "HTTP GET Status = %i, content_length = %lli",
                    esp_http_client_get_status_code(client),
                    esp_http_client_get_content_length(client));
            /*
            char responseBuffer[esp_http_client_get_content_length(client)]; //foam code
            esp_http_client_read(client, *responseBuffer, esp_http_client_get_content_length(client));
            ESP_LOGI(TAG2, "money: %s", responseBuffer);
            */
            ESP_LOGI(TAG2, "Response data: %.*s", request_data.len, request_data.data);

            
            // Parse the JSON data
            //cJSON *root = cJSON_Parse(request_data.data);
            //if (root != NULL) {
                // Extract the "Rain" value
                //cJSON *daily_forecasts = cJSON_GetObjectItem(root, "DailyForecasts");
                //if (cJSON_IsArray(daily_forecasts)) {
                    //cJSON *daily_data = cJSON_GetArrayItem(daily_forecasts, 0);
                    //cJSON *rain = cJSON_GetObjectItem(daily_data, "Rain");
                    //if (cJSON_IsObject(rain)) {
                        //double rain_value = cJSON_GetObjectItem(rain, "Value")->valuedouble;
                        //printf("Rain value: %.2f mm\n", rain_value);
                    //}
                //}

                // Free allocated memory
                //cJSON_Delete(root);


                //return 0;

                /*
                cJSON *root = cJSON_Parse(request_data.data);
                if (root != NULL) {
                    process_json(root);
                    cJSON_Delete(root);
                
                } else {
                    ESP_LOGE(TAG2, "Error parsing JSON: %s", cJSON_GetErrorPtr());
                }
                */
            //} else {
                // Log cJSON parsing error
                //ESP_LOGE(TAG2, "Error parsing JSON: %s", cJSON_GetErrorPtr());
            //}
            

        } else {
            // Log HTTP status code if not OK
            ESP_LOGE(TAG2, "HTTP GET request failed with status code: %d", esp_http_client_get_status_code(client));
        }
    } else {
        ESP_LOGE(TAG2, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    free(request_data.data);
    //closes the connection and frees all memory allocated to http client instance
    esp_http_client_cleanup(client);
    //deletes the task
    vTaskDelete(NULL);
}

void app_main(void)
{
    //esp_err_t is a signed integer type. A majority of ESP-IDF specific function use esp_err_t type to return runtime error codes
    //nvs_flash_init initializes NVS partition (flash memory). NVS holds code even after esp32 is turned off
    //nvs_flash_init returns ESP_OK if successful, if not, returns an error code defined in nvs.h 
    esp_err_t ret = nvs_flash_init();

    //check if NVS partition doesnt contain any empty pages or if it contains data in a format different from current version of code
    //ESP_ERR_NVS_NO_FREE_PAGES and ESP_ERR_NVS_NEW_VERSION_FOUND are error codes found in nvs.h
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      //ESP_ERROR_CHECK is a Macro used to check the error code. If it isn't ESP_OK, aborts the program   
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init(); //initializes NVS
    }

    //ESP_ERROR_CHECK is a Macro used to check the error code. If it isn't ESP_OK, it aborts the program
    ESP_ERROR_CHECK(ret);

    //Logging is better geared toward debugging embedded system then printf
    //print f debugging is more heavyweight, can change the behavior of the program, little use for finding problems w/ memory allocation
    //ESP_LOGE is error (low), ESP_LOGW is warning, ESP_LOGI is info, ESP_LOGD is debug, ESP_LOGV is verbose (high)
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA"); //logs that we are about to set the wifi mode to station
    
    wifi_init_sta(); //calls function to initialize the wifi
    
    //Accuweather url for the http get request (character array used to create string)
    //trash
    //char url[] = "http://dataservice.accuweather.com/forecasts/v1/daily/5day/346069?apikey=pHat28oGnPiyQdCaChTEfuPZkXuzzVff&language=en-us&details=true&metric=true";

    //Creates rtos task "http_request_task" to perform http get request
    //xTaskCreate(http_request_task, url, 8192, NULL, 5, NULL); //might have the parameters switched around
    xTaskCreate(http_request_task, "http_request_task", 8192, NULL, 5, NULL);

    /*
    //spi config
    esp_err_t ret2;
    spi_device_handle_t spi;
    //struct that configures spi bus. Creates an spi bus to send data between microcontrollers and peripherals
    //uses separate clock and data lines, along with a select line to choose the device you wish to talk to 
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO, // GPIO pin for Master In Slave Out (=spi_q) signal, or -1 if not used
        .mosi_io_num = PIN_NUM_MOSI, // GPIO pin for Master Out Slave In (=spi_q) signal, or -1 if not used
        .sclk_io_num = PIN_NUM_CLK, // GPIO pin for SPI Clock signal, or -1 if not used
        .quadwp_io_num = -1, // GPIO pin for WP (Write Protect) signal, or -1 if not used. Not supported
        .quadhd_io_num = -1, // GPIO pin for HD (Hold) signal, or -1 if not used. Not supported
        // Maximum transfer size, in bytes. Defaults to 4092 if 0 when DMA enabled, or to `SOC_SPI_MAXIMUM_BUFFER_SIZE` if DMA is disabled.
        .max_transfer_sz = PARALLEL_LINES * 320 * 2 + 8
    };
    */

    /*
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
    };
    */
} 
