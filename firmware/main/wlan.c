#include "wlan.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_spi_flash.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "settings.h"
#include <string.h>
#include <strings.h>


char wlan_status_string[256];
int wlan_status=0; //0=> not connected; 1=> connected

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about one event 
 * - are we connected to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;

#define TAG "WLAN"

static int s_retry_num = 0;

//This event handler deals with WLAN events and starts the tcp_client_task
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
	    wlan_status=0; //Not connected
        if (s_retry_num < 10) {
            esp_wifi_connect();
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
	    snprintf(wlan_status_string, sizeof(wlan_status_string), "retry to connect to the AP");
        }
        ESP_LOGI(TAG,"connect to the AP fail");
	snprintf(wlan_status_string, sizeof(wlan_status_string), "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:%s",
                 ip4addr_ntoa(&event->ip_info.ip));
	snprintf(wlan_status_string, sizeof(wlan_status_string), "got ip: %s",ip4addr_ntoa(&event->ip_info.ip));
        s_retry_num = 0;
	wlan_status=1; //Connected
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}


void wlan_init_sta()
{
	memset(wlan_status_string, 0, sizeof(wlan_status_string));
    s_wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config={
	    .sta={
		    .ssid="",
		    .password=""
	    }
    };
    ESP_LOGI(TAG, "strncpy");
    strncpy((char*)wifi_config.sta.ssid, get_setting("SSID"), 32);
    strncpy((char*)wifi_config.sta.password, get_setting("PWD"), sizeof(wifi_config.sta.password));
    ESP_LOGI(TAG, "strncpy finished");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:\"%s\" password:\"%s\"", wifi_config.sta.ssid, wifi_config.sta.password);
}

