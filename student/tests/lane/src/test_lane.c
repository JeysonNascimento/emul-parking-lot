/*
 * Testes da máquina de estados da faixa (lane.c): partida e assistência.
 */
#include <errno.h>

#include <zephyr/ztest.h>

#include "adc_sampler.h"
#include "config.h"
#include "hw_fake.h"
#include "lane.h"

/* O amostrador do ADC também é falso aqui: a faixa só vê suas funções. */
FAKE_VALUE_FUNC(bool, adc_sampler_failed);
FAKE_VALUE_FUNC(bool, adc_sampler_take, enum lane_id, uint16_t *);

#define L LANE_EXIT

static struct lane lane;

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	hw_fake_reset();
	RESET_FAKE(adc_sampler_failed);
	RESET_FAKE(adc_sampler_take);
	lane_init(&lane, L);
}

ZTEST_SUITE(lane, NULL, NULL, before, NULL, NULL);

/* Partida completa: vermelho e conferência dos fins de curso. */
static void boot(void)
{
	lane_start(&lane, 0);
	lane_step(&lane, STARTUP_SETTLE_MS);
}

ZTEST(lane, test_boot_waits_before_checking_limits)
{
	fake_in[L][HW_IN_LOWER] = 0; /* entradas ainda não acomodadas */
	lane_start(&lane, 0);
	lane_step(&lane, STARTUP_SETTLE_MS - 1);
	zassert_equal(lane.state, LANE_BOOT);
	zassert_true(fake_out[L][HW_OUT_RED]);
	zassert_equal(fake_out_on_count[L][HW_OUT_BUZZER], 0);

	fake_in[L][HW_IN_LOWER] = 1; /* agora o modelo chegou */
	lane_step(&lane, STARTUP_SETTLE_MS);
	zassert_equal(lane.state, LANE_IDLE);
}

ZTEST(lane, test_start_ok_is_idle_red_no_buzzer)
{
	boot();
	zassert_equal(lane.state, LANE_IDLE);
	zassert_true(fake_out[L][HW_OUT_RED]);
	zassert_false(fake_out[L][HW_OUT_GREEN]);
	zassert_equal(fake_out_on_count[L][HW_OUT_BUZZER], 0);
	zassert_equal(fake_out_on_count[L][HW_OUT_ENABLE], 0);
	/* A outra faixa não é tocada. */
	zassert_false(fake_out[LANE_ENTRY][HW_OUT_RED]);
}

ZTEST(lane, test_start_partial_is_locked_with_one_pulse)
{
	fake_in[L][HW_IN_LOWER] = 0;
	boot();
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_true(fake_out[L][HW_OUT_RED]);
	zassert_false(fake_out[L][HW_OUT_ENABLE]);
	zassert_true(fake_out[L][HW_OUT_BUZZER]);

	lane_step(&lane, STARTUP_SETTLE_MS + BUZZER_PULSE_MS - 1);
	zassert_true(fake_out[L][HW_OUT_BUZZER], "pulso ainda em curso");
	lane_step(&lane, STARTUP_SETTLE_MS + BUZZER_PULSE_MS);
	zassert_false(fake_out[L][HW_OUT_BUZZER]);

	/* Muitos passos depois: nenhum pulso a mais, segue travada. */
	for (int t = 200; t < 5000; t += TICK_MS) {
		lane_step(&lane, t);
	}
	zassert_equal(fake_out_on_count[L][HW_OUT_BUZZER], 1);
	zassert_equal(lane.state, LANE_LOCKED);
}

ZTEST(lane, test_contradictory_later_locks_once)
{
	boot();
	fake_in[L][HW_IN_UPPER] = 1;
	lane_on_input(&lane); /* work da EXTI */
	lane_step(&lane, 30);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_equal(lane.barrier.fault, BARRIER_FAULT_CONTRADICTORY);
	zassert_equal(fake_out_on_count[L][HW_OUT_BUZZER], 1);

	/* A falha some: continua travada até o reset. */
	fake_in[L][HW_IN_UPPER] = 0;
	lane_step(&lane, 500);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_equal(fake_out_on_count[L][HW_OUT_BUZZER], 1);
}

ZTEST(lane, test_on_input_stops_motor_at_upper)
{
	boot();
	zassert_ok(barrier_open(&lane.barrier, 0));
	fake_in[L][HW_IN_LOWER] = 0;
	fake_in[L][HW_IN_UPPER] = 1;
	lane_on_input(&lane);
	zassert_false(fake_out[L][HW_OUT_ENABLE]);
}

ZTEST(lane, test_assist_clears_green_even_if_driver_fails)
{
	boot();
	fake_out_err[L][HW_OUT_BUZZER] = -EIO;
	fake_in[L][HW_IN_LOWER] = 0;
	lane_step(&lane, 30);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_false(fake_out[L][HW_OUT_GREEN]);
	zassert_true(fake_out[L][HW_OUT_RED]);
}

ZTEST(lane, test_adc_fault_locks_lane)
{
	boot();
	lane_step(&lane, 30);
	zassert_equal(lane.state, LANE_IDLE);
	adc_sampler_failed_fake.return_val = true;
	lane_step(&lane, 40);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_true(fake_out[L][HW_OUT_RED]);
	zassert_equal(fake_out_on_count[L][HW_OUT_BUZZER], 1);
}
