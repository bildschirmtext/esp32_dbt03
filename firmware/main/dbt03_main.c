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
#include "esp_timer.h"
#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>


// PINOUTS
#define GPIO_OUTPUT_ED    GPIO_NUM_18
#define GPIO_INPUT_SD    GPIO_NUM_19
#define GPIO_INPUT_S GPIO_NUM_21
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_0) | (1ULL<<GPIO_OUTPUT_IO_1))


//WLAN settings
#include "wifi_settings.h"
//Set your WLAN either here or in /wifi_settings.h
//#define EXAMPLE_ESP_WIFI_SSID      "SSID"
//#define EXAMPLE_ESP_WIFI_PASS      "Password"
#define EXAMPLE_ESP_MAXIMUM_RETRY  5


static const char *TAG = "wifi station";

#define LEDC_HS_TIMER          LEDC_TIMER_0
#define LEDC_HS_MODE           LEDC_HIGH_SPEED_MODE
#define LEDC_HS_CH0_GPIO       (18)
#define LEDC_HS_CH0_CHANNEL    LEDC_CHANNEL_0


#define SW_UART_LOW (1)
#define SW_UART_HIGH (0)
#define SW_UART_BSIZE (16)

static int sw_uart_state=-1; //State -1=idle, 0... position in frame
static uint8_t sw_uart_data=0; //data of the current frame
static uint8_t sw_uart_bits=0; //number of bits during sampling period
static uint8_t sw_uart_wp=0;  //write pointer
static uint8_t sw_uart_rp=0; //read pointer
static uint8_t sw_uart_buffer[SW_UART_BSIZE]; //buffer

static int uartc=0;




static void software_uart_callback(void* arg)
{
	uartc=uartc+1;
	int level=gpio_get_level(GPIO_INPUT_SD);
	if (sw_uart_state<0) { //idle state
		if (level==SW_UART_LOW) { //Start Bit
			sw_uart_state=0;
			sw_uart_data=0;
			sw_uart_bits=0;
		}
		return;
	} 
	int bn=sw_uart_state/16; //which bit is it 0=start
	int ib=sw_uart_state%16; //Position within bit
	sw_uart_state=sw_uart_state+1; //count up state
	if ((ib>=6) && (ib<=8)) { //Middle 3 samples within a bit
		if (level==SW_UART_HIGH)
			sw_uart_bits=sw_uart_bits+1; //Count those bits for majority decision
		return;
	}
	if (ib==9) { //After the sampling	
		if ( (bn>=1) && (bn<=8) ) { //Data bits
			sw_uart_data=(sw_uart_data>>1);
			if (sw_uart_bits>=2) sw_uart_data=sw_uart_data | (1 << 7);
			sw_uart_bits=0; //Reset bit counter
		}
		if (bn>=9) { //Stop bit
			if (level==SW_UART_HIGH) { //Stop bit OK
				//Fixme check for full buffer
				sw_uart_buffer[sw_uart_wp]=sw_uart_data;
				sw_uart_wp=(sw_uart_wp+1)%SW_UART_BSIZE;
			}
			sw_uart_state=-1; //set state to idle
		}
	}
	return;
}

int software_uart_read()
{
	if (sw_uart_wp==sw_uart_rp) return -1;
	int d=sw_uart_buffer[sw_uart_rp];
	sw_uart_rp=(sw_uart_rp+1)%SW_UART_BSIZE;
	return d;
}

void init_software_uart()
{
	gpio_config_t io_conf;
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = (1ULL<<GPIO_INPUT_SD);
	io_conf.pull_down_en = 0;
	io_conf.pull_up_en = 0;
	//configure GPIO with the given settings
	gpio_config(&io_conf);
	const esp_timer_create_args_t periodic_timer_args = {
		.callback = &software_uart_callback,
		/* name is optional, but may help identify the timer when debugging */
		.name = "swuart"
	};
	esp_timer_handle_t periodic_timer;
	ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 1000000/1200)); //1200 Hz output rate of the timer
	sw_uart_state=-1;
	sw_uart_wp=0;
	sw_uart_rp=0;
}


void send_gpio_break()
{
	return;
	gpio_config_t io_conf;
	//disable interrupt
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	//set as output mode
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pin_bit_mask = (1ULL<<GPIO_OUTPUT_ED);
	io_conf.pull_down_en = 0;
	io_conf.pull_up_en = 0;
	//configure GPIO with the given settings
	gpio_config(&io_conf);
	gpio_set_level(GPIO_OUTPUT_ED,0);
	vTaskDelay(200/portTICK_PERIOD_MS);
	gpio_set_level(GPIO_OUTPUT_ED,1);
}


//We use UART_1 for sending at 1200bps and UART_2 for receiving at 75bps
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
/*	//RX 75bps Terminal=>IP
	uart_config.baud_rate=75;
	ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart_config));
	ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, UART_PIN_NO_CHANGE, GPIO_INPUT_SD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
	ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, 128 * 2, 0, 0, NULL, 0));*/
}

//The LED PWM timer is used to simulate the tones
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

//This function sets the LED PWM timer to a certain frequency
// frq=0 stops the timer
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
		ledc_stop(ledc_channel.speed_mode, ledc_channel.channel, 0);
	} else {

		ledc_set_freq(ledc_channel.speed_mode, ledc_channel.channel, frq);
		ledc_channel_config(&ledc_channel);
		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 7);
		ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
	}
}


//This is the task that connects the TCP socket to the serial lines
static void tcp_client_task(void *pvParameters)
{

	
	ESP_LOGE(TAG, "Connecting\n");
	char addr_str[128];
	int addr_family;
	int ip_protocol;
	
	beep_led(440);
	vTaskDelay(500/portTICK_PERIOD_MS);
	beep_led(0);
	vTaskDelay(500/portTICK_PERIOD_MS);
	beep_led(1300);
//	vTaskDelay(1650/portTICK_PERIOD_MS);
	vTaskDelay(500/portTICK_PERIOD_MS);
	beep_led(0);


	init_software_uart();
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
		/* set recv timeout (100 ms) */
		int opt = 100;
		lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &opt, sizeof(int));

		while (1) {
			char tx_buffer[8]; //Terminal=>IP
			int tx_len = software_uart_read();
			if (tx_len>=0) {
				ESP_LOGE(TAG, "read %d from UART\n", tx_len);
				tx_buffer[0]=tx_len;
				int err = send(sock, tx_buffer, 1, 0);
				if (err < 0) {
					ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
					break;
				}
			}

			char rx_buffer[8]; //IP=>Terminal
			int rx_len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, MSG_DONTWAIT);
			// Error occurred during receiving
			if (rx_len < 0) {
				if (errno!=11) {
					ESP_LOGE(TAG, "recv failed: errno %d", errno);
					break;
				}
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




/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about one event 
 * - are we connected to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;


static int s_retry_num = 0;

//This event handler deals with WLAN events and starts the tcp_client_task
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
	printf("Waiting for S to go now.\n");
	//Configure S-Input which starts up the modem
	gpio_config_t io_conf;
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = (1ULL<<GPIO_INPUT_S);
	io_conf.pull_down_en = 0;
	io_conf.pull_up_en = 0;
	gpio_config(&io_conf);
	while ((gpio_get_level(GPIO_INPUT_S))!=0){
		vTaskDelay(100/portTICK_PERIOD_MS);
	}

	vTaskDelay(1000/portTICK_PERIOD_MS);
	beep_led(0);
	vTaskDelay(2000/portTICK_PERIOD_MS);

	//Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);



	wifi_init_sta();

	//Wait to shut down
	while ((gpio_get_level(GPIO_INPUT_S))==0){
		vTaskDelay(100/portTICK_PERIOD_MS);
	}

	printf("Restarting now.\n");
	fflush(stdout);
	esp_restart();
}
