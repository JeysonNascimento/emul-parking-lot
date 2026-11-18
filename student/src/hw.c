/*
 * Camada fina de hardware: GPIO, EXTI e tempo.
 *
 * Os pinos vêm dos aliases do devicetree da placa parking_lot_u5; o código
 * nunca usa número de pino.
 */
#include "hw.h"

#include <errno.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

/* Saídas, indexadas por [faixa][enum hw_out]. */
static const struct gpio_dt_spec outputs[LANE_COUNT][HW_OUT_COUNT] = {
	[LANE_ENTRY] = {
		[HW_OUT_ENABLE] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_motor_enable), gpios),
		[HW_OUT_DIRECTION] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_motor_direction), gpios),
		[HW_OUT_RED] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_light_red), gpios),
		[HW_OUT_GREEN] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_light_green), gpios),
		[HW_OUT_BUZZER] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_buzzer), gpios),
	},
	[LANE_EXIT] = {
		[HW_OUT_ENABLE] = GPIO_DT_SPEC_GET(DT_ALIAS(exit_motor_enable), gpios),
		[HW_OUT_DIRECTION] = GPIO_DT_SPEC_GET(DT_ALIAS(exit_motor_direction), gpios),
		[HW_OUT_RED] = GPIO_DT_SPEC_GET(DT_ALIAS(exit_light_red), gpios),
		[HW_OUT_GREEN] = GPIO_DT_SPEC_GET(DT_ALIAS(exit_light_green), gpios),
		[HW_OUT_BUZZER] = GPIO_DT_SPEC_GET(DT_ALIAS(exit_buzzer), gpios),
	},
};

/* Entradas, indexadas por [faixa][enum hw_in]. */
static const struct gpio_dt_spec inputs[LANE_COUNT][HW_IN_COUNT] = {
	[LANE_ENTRY] = {
		[HW_IN_UPPER] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_limit_upper), gpios),
		[HW_IN_LOWER] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_limit_lower), gpios),
		[HW_IN_BEAM] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_beam), gpios),
	},
	[LANE_EXIT] = {
		[HW_IN_UPPER] = GPIO_DT_SPEC_GET(DT_ALIAS(exit_limit_upper), gpios),
		[HW_IN_LOWER] = GPIO_DT_SPEC_GET(DT_ALIAS(exit_limit_lower), gpios),
		[HW_IN_BEAM] = GPIO_DT_SPEC_GET(DT_ALIAS(exit_beam), gpios),
	},
};

/* ADC4: um conversor para os dois sensores de distância. */
#define DIST_ENTRY DT_ALIAS(entry_distance)
#define DIST_EXIT DT_ALIAS(exit_distance)

static const struct device *const adc_dev = DEVICE_DT_GET(DT_PARENT(DIST_ENTRY));

static const struct adc_channel_cfg adc_cfgs[LANE_COUNT] = {
	[LANE_ENTRY] = ADC_CHANNEL_CFG_DT(DIST_ENTRY),
	[LANE_EXIT] = ADC_CHANNEL_CFG_DT(DIST_EXIT),
};

static const uint8_t adc_resolution[LANE_COUNT] = {
	[LANE_ENTRY] = DT_PROP(DIST_ENTRY, zephyr_resolution),
	[LANE_EXIT] = DT_PROP(DIST_EXIT, zephyr_resolution),
};

/*
 * REQ-10: estáticos de propósito. O driver escreve no buffer e sinaliza
 * quando termina, mesmo que seja tarde; por isso nada disto pode estar na
 * pilha de uma função.
 */
static uint16_t adc_buffer;
static struct adc_sequence adc_seq;
static struct k_poll_signal adc_signal;

/*
 * UARTs: a interrupção de recepção copia os bytes para um ring buffer por
 * UART; o laço principal os retira com hw_uart_read(). Um produtor (ISR) e
 * um consumidor (laço) por buffer: o ring_buf do Zephyr dispensa trava
 * nesse caso. Se o buffer encher, os bytes excedentes se perdem e o quadro
 * corrompido é descartado pelo montador de linhas.
 */
#define UART_RX_BUF 256

struct uart_chan {
	const struct device *dev;
	struct ring_buf rb;
	uint8_t storage[UART_RX_BUF];
};

static struct uart_chan uarts[HW_UART_COUNT] = {
	[HW_UART_CAMERA_ENTRY] = {.dev = DEVICE_DT_GET(DT_ALIAS(entry_camera))},
	[HW_UART_CAMERA_EXIT] = {.dev = DEVICE_DT_GET(DT_ALIAS(exit_camera))},
	[HW_UART_PAYMENT] = {.dev = DEVICE_DT_GET(DT_ALIAS(payment_terminal))},
};

/* Contexto de interrupção: esvazia a FIFO de recepção no ring buffer. */
static void uart_isr(const struct device *dev, void *user_data)
{
	struct uart_chan *ch = user_data;
	uint8_t chunk[16];

	uart_irq_update(dev);
	while (uart_irq_rx_ready(dev) > 0) {
		int n = uart_fifo_read(dev, chunk, sizeof(chunk));

		if (n <= 0) {
			break;
		}
		(void)ring_buf_put(&ch->rb, chunk, (uint32_t)n);
	}
}

static int setup_uart(struct uart_chan *ch)
{
	int rc;

	if (!device_is_ready(ch->dev)) {
		return -ENODEV;
	}
	ring_buf_init(&ch->rb, sizeof(ch->storage), ch->storage);
	rc = uart_irq_callback_user_data_set(ch->dev, uart_isr, ch);
	if (rc != 0) {
		return rc;
	}
	uart_irq_rx_enable(ch->dev);
	return 0;
}

/* Um callback de GPIO por entrada; guarda a faixa a que pertence. */
struct input_cb {
	struct gpio_callback cb;
	enum lane_id lane;
};

static struct input_cb input_cbs[LANE_COUNT][HW_IN_COUNT];

/* Uma work por faixa: tira o tratamento do contexto de interrupção. */
struct lane_work {
	struct k_work work;
	enum lane_id lane;
};

static struct lane_work lane_works[LANE_COUNT];
static hw_input_handler_t input_handler;

/* Contexto de work: repassa para a lógica (que para o motor se preciso). */
static void lane_work_fn(struct k_work *work)
{
	struct lane_work *lw = CONTAINER_OF(work, struct lane_work, work);

	input_handler(lw->lane);
}

/* Contexto de interrupção (EXTI): só agenda a work, nada mais. */
static void input_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	struct input_cb *icb = CONTAINER_OF(cb, struct input_cb, cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	(void)k_work_submit(&lane_works[icb->lane].work);
}

static int setup_input(enum lane_id lane, enum hw_in in)
{
	const struct gpio_dt_spec *spec = &inputs[lane][in];
	struct input_cb *icb = &input_cbs[lane][in];
	int rc;

	if (!gpio_is_ready_dt(spec)) {
		return -ENODEV;
	}
	rc = gpio_pin_configure_dt(spec, GPIO_INPUT);
	if (rc != 0) {
		return rc;
	}
	icb->lane = lane;
	gpio_init_callback(&icb->cb, input_isr, BIT(spec->pin));
	rc = gpio_add_callback_dt(spec, &icb->cb);
	if (rc != 0) {
		return rc;
	}
	/* As duas bordas: interessa tanto acender quanto apagar. */
	return gpio_pin_interrupt_configure_dt(spec, GPIO_INT_EDGE_BOTH);
}

int hw_init(hw_input_handler_t handler)
{
	int rc;

	input_handler = handler;

	/* Primeiro as saídas, todas inativas: motor parado, sinais apagados. */
	for (int lane = 0; lane < LANE_COUNT; lane++) {
		for (int out = 0; out < HW_OUT_COUNT; out++) {
			const struct gpio_dt_spec *spec = &outputs[lane][out];

			if (!gpio_is_ready_dt(spec)) {
				return -ENODEV;
			}
			rc = gpio_pin_configure_dt(spec, GPIO_OUTPUT_INACTIVE);
			if (rc != 0) {
				return rc;
			}
		}
	}

	/* ADC4 e os dois canais de distância. */
	if (!device_is_ready(adc_dev)) {
		return -ENODEV;
	}
	for (int lane = 0; lane < LANE_COUNT; lane++) {
		rc = adc_channel_setup(adc_dev, &adc_cfgs[lane]);
		if (rc != 0) {
			return rc;
		}
	}
	k_poll_signal_init(&adc_signal);

	/* UARTs dos equipamentos, com recepção por interrupção. */
	for (int u = 0; u < HW_UART_COUNT; u++) {
		rc = setup_uart(&uarts[u]);
		if (rc != 0) {
			return rc;
		}
	}

	/* Depois as entradas e as interrupções. */
	for (int lane = 0; lane < LANE_COUNT; lane++) {
		lane_works[lane].lane = (enum lane_id)lane;
		k_work_init(&lane_works[lane].work, lane_work_fn);
		for (int in = 0; in < HW_IN_COUNT; in++) {
			rc = setup_input((enum lane_id)lane, (enum hw_in)in);
			if (rc != 0) {
				return rc;
			}
		}
	}
	return 0;
}

int hw_out_set(enum lane_id lane, enum hw_out out, bool on)
{
	return gpio_pin_set_dt(&outputs[lane][out], on ? 1 : 0);
}

int hw_in_get(enum lane_id lane, enum hw_in in)
{
	return gpio_pin_get_dt(&inputs[lane][in]);
}

int hw_adc_start(enum lane_id lane)
{
	adc_seq = (struct adc_sequence){
		.channels = BIT(adc_cfgs[lane].channel_id),
		.buffer = &adc_buffer,
		.buffer_size = sizeof(adc_buffer),
		.resolution = adc_resolution[lane],
	};
	k_poll_signal_reset(&adc_signal);
	return adc_read_async(adc_dev, &adc_seq, &adc_signal);
}

int hw_adc_poll(uint16_t *raw)
{
	unsigned int signaled;
	int result;

	k_poll_signal_check(&adc_signal, &signaled, &result);
	if (signaled == 0U) {
		return -EINPROGRESS;
	}
	if (result != 0) {
		return result;
	}
	*raw = adc_buffer;
	return 0;
}

int hw_uart_write(enum hw_uart uart, const char *data, size_t len)
{
	const struct device *dev = uarts[uart].dev;

	for (size_t i = 0; i < len; i++) {
		uart_poll_out(dev, (unsigned char)data[i]);
	}
	return 0;
}

size_t hw_uart_read(enum hw_uart uart, uint8_t *buf, size_t max)
{
	return ring_buf_get(&uarts[uart].rb, buf, (uint32_t)max);
}

int64_t hw_now_ms(void)
{
	return k_uptime_get();
}

void hw_sched_lock(void)
{
	k_sched_lock();
}

void hw_sched_unlock(void)
{
	k_sched_unlock();
}
