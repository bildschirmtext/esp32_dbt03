#include "if_dbt03.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#include "driver/ledc.h"
#include "esp_timer.h"

// PINOUTS
#define GPIO_OUTPUT_ED    GPIO_NUM_18
#define GPIO_INPUT_SD    GPIO_NUM_19
#define GPIO_INPUT_S GPIO_NUM_21
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_0) | (1ULL<<GPIO_OUTPUT_IO_1))


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

esp_timer_handle_t periodic_timer;//Timer for software UART (75bps)

/*
 * This function is called 1200 times per second
 * It's there to manage the 75bps uplink
 */
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

int software_uart_read(int x)
{
	if (sw_uart_wp==sw_uart_rp) return -1;
	int d=sw_uart_buffer[sw_uart_rp];
	sw_uart_rp=(sw_uart_rp+1)%SW_UART_BSIZE;
	return d;
}

void reset_software_uart()
{
	sw_uart_state=-1;
	sw_uart_wp=0;
	sw_uart_rp=0;
}

static int dbt03_swuart_inited=0;

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
	reset_software_uart();
	if (dbt03_swuart_inited!=0) return;
	dbt03_swuart_inited=1;
	const esp_timer_create_args_t periodic_timer_args = {
		.callback = &software_uart_callback,
		/* name is optional, but may help identify the timer when debugging */
		.name = "swuart"
	};
	ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 1000000/1200)); //1200 Hz output rate of the timer
}


void send_gpio_break()
{
	return;
}


static int dbt03_uart_inited=0;

//We use UART_1 for sending at 1200bps and software UART at 75bps
void init_uart()
{
	//if (dbt03_uart_inited!=0) return;
	printf("init_uart\n");
	uart_config_t uart_config = {
		.baud_rate = 1200,
		.data_bits = UART_DATA_8_BITS,
		.parity    = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	};
	//TX 1200bps IP=>Terminal
	if (dbt03_uart_inited==0) ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
	//if (dbt03_uart_inited==0) 
	ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, GPIO_OUTPUT_ED, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
	if (dbt03_uart_inited==0) ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 128 * 2, 0, 0, NULL, 0));
	dbt03_uart_inited=1;
}

int uart_write(int x)
{
	char rx_buffer[1];
	rx_buffer[0]=x;
	uart_write_bytes(UART_NUM_1, (const char *) rx_buffer, 1);
	return -1;
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

void if_dbt03_init()
{
	init_led();
	beep_led(440);
	printf("if_dbt03_init.\n");
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
	beep_led(440);
	vTaskDelay(500/portTICK_PERIOD_MS);
	beep_led(0);
	vTaskDelay(500/portTICK_PERIOD_MS);
	beep_led(1300);
	vTaskDelay(500/portTICK_PERIOD_MS);
	beep_led(0);
	init_software_uart();
	init_uart();
	vTaskDelay(500/portTICK_PERIOD_MS);
}

void if_dbt03_deinit()
{
	//Fixme, disable SW UART
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
	vTaskDelay(500/portTICK_PERIOD_MS);
}




int if_dbt03_status(int x)
{
	if (x==1) if_dbt03_init();
	if (x==2) if_dbt03_deinit();
	if ((gpio_get_level(GPIO_INPUT_S))==0) return 0;
	return 1;
}

int if_dbt03_write(int x)
{
	return uart_write(x);
}

int if_dbt03_read(int x)
{
	while (1==1) {
		int c=software_uart_read(x);
		if (c>=0) return c;
		if (x==0) return -1;
		vTaskDelay(100/portTICK_PERIOD_MS);;
		if (if_dbt03_status(0)==1) return -1;
	}
}

io_type_t if_dbt03={
	.in=if_dbt03_read,
	.out=if_dbt03_write,
	.status=if_dbt03_status
};
