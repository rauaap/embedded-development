/*
Exercise for week 2.
Implemented features:
	- Cycling led with red, yellow and green light in normal state.
	- Pausing with button 0.
	- Toggling different colors in paused state with buttons 1, 2 and 3.
	- Enabling and disabling blink state where yellow light blinks every second with button 4.

Looking to get 5 as all features were completed.
*/

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/devicetree.h>


void reset_leds();

enum State {
	RED,
	YELLOW,
	GREEN,
	PAUSE,
	BLINK
} global_state = RED;


#define NUM_BUTTONS 5

// We need to know which leds are on when we want to 
// toggle different led colors. Their status is checked against these.
#define LED_RED 2
#define LED_YELLOW 3
#define LED_GREEN 1

static struct gpio_callback button_data[NUM_BUTTONS];


/* Define gpio_dt_spec for each button */
static const struct gpio_dt_spec buttons[NUM_BUTTONS] = {
    GPIO_DT_SPEC_GET(DT_NODELABEL(button_1_vol_dn), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(button_2_vol_up), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(button3), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(button4), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(button5), gpios),
};


// Led pin configurations.
// Put them in a nice struct that can also be iterated with a for loop.
// Comes in handy during initialization and resetting.
static const union {
	struct {
		struct gpio_dt_spec red; 
		struct gpio_dt_spec green; 
		struct gpio_dt_spec blue; 
	};
	struct gpio_dt_spec array[3];
} leds = {
	.red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
	.green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),
	.blue = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios),
};


// Button 0 handler. Responsible for pausing any actions and restoring the previous state
// from paused state.
void button_0_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
	static enum State prev_state = RED;

	if (global_state != PAUSE) {
		prev_state = global_state;
		global_state = PAUSE;
	}
	else {
		global_state = prev_state == BLINK ? RED : prev_state;
	}
}


int get_current_led_state() {
	return (
		(gpio_pin_get_dt(&leds.red) << 1) | 
		gpio_pin_get_dt(&leds.green)
	);
}


// Toggles the red light in paused state.
void button_1_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
	if (global_state != PAUSE) {
		return;
	}
	
	if (get_current_led_state() != LED_RED) {
		reset_leds();
	}

	gpio_pin_toggle_dt(&leds.red);
}


// Toggles the yellow light in paused state.
void button_2_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
	if (global_state != PAUSE) {
		return;
	}
	
	if (get_current_led_state() != LED_YELLOW) {
		reset_leds();
	}

	gpio_pin_toggle_dt(&leds.red);
	gpio_pin_toggle_dt(&leds.green);
}


// Toggles the green light in paused state.
void button_3_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
	if (global_state != PAUSE) {
		return;
	}
	
	if (get_current_led_state() != LED_GREEN) {
		reset_leds();
	}

	gpio_pin_toggle_dt(&leds.green);
}


// Starts the blinking sequence
void button_4_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
	global_state = global_state == BLINK ? PAUSE : BLINK;

	if (global_state != BLINK) {
		reset_leds();
	}
}


typedef void(*button_handler_fptr)(const struct device*, struct gpio_callback*, uint32_t);


button_handler_fptr button_handlers[NUM_BUTTONS] = {
	button_0_handler,
	button_1_handler,
	button_2_handler,
	button_3_handler,
	button_4_handler
};


int init_buttons() {
	for (int i = 0; i < sizeof(buttons)/sizeof(buttons[0]); i++) {	
		if (!gpio_is_ready_dt(buttons + i)) {
			printk("Error: button %d is not ready\n", i);
			return -1;
		}

		if (gpio_pin_configure_dt(buttons + i, GPIO_INPUT) != 0) {
			printk("Error: failed to configure pin for button %d\n", i);
			return -1;
		}

		if (gpio_pin_interrupt_configure_dt(buttons + i, GPIO_INT_EDGE_TO_ACTIVE) != 0) {
			printk("Error: failed to configure interrupt on pin for button %d\n", i);
			return -1;
		}

		gpio_init_callback(button_data + i, button_handlers[i], BIT(buttons[i].pin));
		gpio_add_callback(buttons[i].port, button_data + i);
		printk("Set up button %d ok\n", i);
	}

	return 0;
}


// Led thread initialization parameters.
#define STACKSIZE 500
#define PRIORITY 5


// Forward declarations for led task functions.
void red_led_task(void*, void*, void*);
void yellow_led_task(void*, void*, void*);
void green_led_task(void*, void*, void*);
void blink_task(void*, void*, void*);


// Define the threads for the led tasks
K_THREAD_DEFINE(red_thread, STACKSIZE, red_led_task, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(yellow_thread, STACKSIZE, yellow_led_task, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(green_thread, STACKSIZE, green_led_task, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(blink_thread, STACKSIZE, blink_task, NULL, NULL, NULL, PRIORITY, 0, 0);


int init_leds() {
	// Led pin initialization
	for (int i = 0; i < sizeof(leds.array) / sizeof(leds.array[0]); i++) {
		// Have to set pin also as an input if you
		// want to read its state:
		// https://github.com/zephyrproject-rtos/zephyr/issues/48058
		// Brilliant.
		int ret = gpio_pin_configure_dt(
			leds.array + i,
			GPIO_OUTPUT_ACTIVE | GPIO_INPUT
		);

		if (ret < 0) {
			printk("Error: Led configure failed\n");		
			return ret;
		}

		// set led off
		gpio_pin_set_dt(leds.array + i, 0);
	}

	printk("Led initialized ok\n");
	return 0;
}


void reset_leds() {
	for (int i = 0; i < sizeof(leds.array) / sizeof(leds.array[0]); i++)
		gpio_pin_set_dt(leds.array + i, 0);
}


void red_led_task(void*, void*, void*) {
	printk("Red led thread started\n");

	while (true) {
		if (global_state != RED) {
			continue;
		}

		reset_leds();
		gpio_pin_set_dt(&leds.red, 1);	
		k_sleep(K_SECONDS(1));

		// Move into the next state only if the state wasn't changed
		// to pause or blink during the wait.
		if (global_state != PAUSE && global_state != BLINK) {
			global_state = YELLOW;
		}	
	}
}


void yellow_led_task(void*, void*, void*) {
	printk("Yellow led thread started\n");

	while (true) {
		if (global_state != YELLOW) {
			continue;
		}

		reset_leds();
		gpio_pin_set_dt(&leds.red, 1);	
		gpio_pin_set_dt(&leds.green, 1);	
		k_sleep(K_SECONDS(1));

		// Move into the next state only if the state wasn't changed
		// to pause or blink during the wait.
		if (global_state != PAUSE && global_state != BLINK) {
			global_state = GREEN;
		}
	}
}


void green_led_task(void*, void*, void*) {
	printk("Green led thread started\n");

	while (true) {
		if (global_state != GREEN) {
			continue;
		}

		reset_leds();
		gpio_pin_set_dt(&leds.green, 1);	
		k_sleep(K_SECONDS(1));

		// Move into the next state only if the state wasn't changed
		// to pause or blink during the wait.
		if (global_state != PAUSE && global_state != BLINK) {
			global_state = RED;
		}	
	}
}


void blink_task(void*, void*, void*) {
	while(true) {
		if (global_state != BLINK) {
			continue;
		}

		if (get_current_led_state() == LED_YELLOW) {
			reset_leds();
		}
		else {
			gpio_pin_set_dt(&leds.red, 1);
			gpio_pin_set_dt(&leds.green, 1);
		}

		k_sleep(K_SECONDS(1));
	}
}


// Main program
int main(void) {
	init_leds();
	init_buttons();
	return 0;
}

