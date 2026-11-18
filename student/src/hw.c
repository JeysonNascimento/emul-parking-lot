/*
 * Camada fina de hardware: GPIO, EXTI e tempo.
 *
 * Os pinos vêm dos aliases do devicetree da placa parking_lot_u5; o código
 * nunca usa número de pino.
 */
#include "hw.h"

#include <errno.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

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
