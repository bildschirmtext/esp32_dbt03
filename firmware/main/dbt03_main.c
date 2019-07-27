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
#include "wlan.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include <string.h>




#include "applications.h"
#include "settings.h"
#include "interfaces.h"
#include "if_dbt03.h"







void app_main()
{
	printf("Hello world!\n");

	//Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	settings_init();
	wlan_init_sta();


	xTaskCreate(terminal_task, "dbt03", 4096, &if_dbt03 , 5, NULL);

	while (1==1){
	//	printf("500 ms delay\n");
		vTaskDelay(500/portTICK_PERIOD_MS);
	}

	printf("Restarting now.\n");
	fflush(stdout);
	//esp_restart();
}
