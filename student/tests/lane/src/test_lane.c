/*
 * Testes da máquina de estados da faixa (lane.c): partida e assistência.
 */
#include <errno.h>

#include <zephyr/ztest.h>

#include "config.h"
#include "hw_fake.h"
#include "lane.h"

#define L LANE_EXIT

static struct lane lane;

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	hw_fake_reset();
	lane_init(&lane, L);
}

ZTEST_SUITE(lane, NULL, NULL, before, NULL, NULL);

ZTEST(lane, test_start_ok_is_idle_red_no_buzzer)
{
	lane_start(&lane, 0);
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
	lane_start(&lane, 0);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_true(fake_out[L][HW_OUT_RED]);
	zassert_false(fake_out[L][HW_OUT_ENABLE]);
	zassert_true(fake_out[L][HW_OUT_BUZZER]);

	lane_step(&lane, BUZZER_PULSE_MS - 1);
	zassert_true(fake_out[L][HW_OUT_BUZZER], "pulso ainda em curso");
	lane_step(&lane, BUZZER_PULSE_MS);
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
	lane_start(&lane, 0);
	fake_in[L][HW_IN_UPPER] = 1;
	lane_on_input(&lane); /* work da EXTI */
	lane_step(&lane, 10);
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
	lane_start(&lane, 0);
	zassert_ok(barrier_open(&lane.barrier, 0));
	fake_in[L][HW_IN_LOWER] = 0;
	fake_in[L][HW_IN_UPPER] = 1;
	lane_on_input(&lane);
	zassert_false(fake_out[L][HW_OUT_ENABLE]);
}

ZTEST(lane, test_assist_clears_green_even_if_driver_fails)
{
	lane_start(&lane, 0);
	fake_out_err[L][HW_OUT_BUZZER] = -EIO;
	fake_in[L][HW_IN_LOWER] = 0;
	lane_step(&lane, 10);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_false(fake_out[L][HW_OUT_GREEN]);
	zassert_true(fake_out[L][HW_OUT_RED]);
}
