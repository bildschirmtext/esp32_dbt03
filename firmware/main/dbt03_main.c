/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_spi_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/ledc.h"
#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>


#define GPIO_OUTPUT_ED    GPIO_NUM_18
#define GPIO_INPUT_SD    GPIO_NUM_19
#define GPIO_INPUT_START GPIO_NUM_20
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_0) | (1ULL<<GPIO_OUTPUT_IO_1))

static const char *TAG = "wifi station";

#define LEDC_HS_TIMER          LEDC_TIMER_0
#define LEDC_HS_MODE           LEDC_HIGH_SPEED_MODE
#define LEDC_HS_CH0_GPIO       (18)
#define LEDC_HS_CH0_CHANNEL    LEDC_CHANNEL_0


void init_uart()
{
	printf("init_uart\n");
	uart_config_t uart_config = {
		.baud_rate = 1200,
		.data_bits = UART_DATA_8_BITS,
		.parity    = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	};
	//TX 1200bps IP=>Terminal
	ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
	ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, GPIO_OUTPUT_ED, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
	ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 128 * 2, 0, 0, NULL, 0));
	//RX 75bps Terminal=>IP
	uart_config.baud_rate=75;
	ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart_config));
	ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, UART_PIN_NO_CHANGE, GPIO_INPUT_SD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
	ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, 128 * 2, 0, 0, NULL, 0));
}

void init_led()
{
	ledc_timer_config_t ledc_timer = {
		.duty_resolution = LEDC_TIMER_4_BIT, // resolution of PWM duty
		.freq_hz = 440,                      // frequency of PWM signal
		.speed_mode = LEDC_HS_MODE,           // timer mode
		.timer_num = LEDC_HS_TIMER            // timer index
	};
	
	ledc_timer_config(&ledc_timer);
}

void beep_led(const int frq)
{

	ledc_channel_config_t ledc_channel = {
		.channel    = LEDC_HS_CH0_CHANNEL,
		.duty       = 0,
		.gpio_num   = LEDC_HS_CH0_GPIO,
		.speed_mode = LEDC_HS_MODE,
		.hpoint     = 0,
		.timer_sel  = LEDC_HS_TIMER
	};

	if (frq==0) {
		ledc_stop(ledc_channel.speed_mode, ledc_channel.channel, 1);
	} else {

		ledc_set_freq(ledc_channel.speed_mode, ledc_channel.channel, frq);
		ledc_channel_config(&ledc_channel);
		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 7);
		ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
	}
}



static void tcp_client_task(void *pvParameters)
{

	char addr_str[128];
	int addr_family;
	int ip_protocol;
	beep_led(0);
	vTaskDelay(1000 /portTICK_PERIOD_MS);
	beep_led(1700);

	vTaskDelay(500 / portTICK_PERIOD_MS);
	beep_led(0);
	vTaskDelay(1000 /portTICK_PERIOD_MS);
	init_uart();

	while (1) {
		struct sockaddr_in dest_addr;
		dest_addr.sin_addr.s_addr = inet_addr("195.201.94.166");
		dest_addr.sin_family = AF_INET;
		dest_addr.sin_port = htons(20000);
		addr_family = AF_INET;
		ip_protocol = IPPROTO_IP;
		inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

		int sock =  socket(addr_family, SOCK_STREAM, ip_protocol);
		if (sock < 0) {
			ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
			break;
		}
		ESP_LOGI(TAG, "Socket created, connecting to ");

		int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
		if (err != 0) {
			ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
			break;
		}
		ESP_LOGI(TAG, "Successfully connected");

		while (1) {
			char tx_buffer[8]; //Terminal=>IP
			int tx_len = uart_read_bytes(UART_NUM_2, (unsigned char*) tx_buffer, sizeof(tx_buffer), 0);
			int err = send(sock, tx_buffer, tx_len, 0);
			if (err < 0) {
				ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
				break;
			}

			char rx_buffer[8]; //IP=>Terminal
			int rx_len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
			// Error occurred during receiving
			if (rx_len < 0) {
				ESP_LOGE(TAG, "recv failed: errno %d", errno);
				break;
			}
			// Data received
			else {
				uart_write_bytes(UART_NUM_1, (const char *) rx_buffer, rx_len);
			}

			vTaskDelay(20 / portTICK_PERIOD_MS);
		}

		if (sock != -1) {
			ESP_LOGE(TAG, "Shutting down socket and restarting...");
			shutdown(sock, 0);
			close(sock);
		}
	}
	vTaskDelete(NULL);
}



/* The examples use WiFi configuration that you can set via 'make menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      "SSID"
#define EXAMPLE_ESP_WIFI_PASS      "Password"
#define EXAMPLE_ESP_MAXIMUM_RETRY  5

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about one event 
 * - are we connected to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;


static int s_retry_num = 0;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:%s",
                 ip4addr_ntoa(&event->ip_info.ip));
        s_retry_num = 0;
	 xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, NULL);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}


void wifi_init_sta()
{
    s_wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}



void app_main()
{
	printf("Hello world!\n");

	init_led();
	beep_led(440);

	//Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);



	wifi_init_sta();
	vTaskDelay(1500/ portTICK_PERIOD_MS);

	for (int i = 1000; i >= 0; i--) {
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
	printf("Restarting now.\n");
	fflush(stdout);
	esp_restart();
}
