/* Implementação dos falsos de hw.h (ver hw_fake.h). */
#include "hw_fake.h"

#include <string.h>

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(int, hw_init, hw_input_handler_t);
DEFINE_FAKE_VALUE_FUNC(int, hw_out_set, enum lane_id, enum hw_out, bool);
DEFINE_FAKE_VALUE_FUNC(int, hw_in_get, enum lane_id, enum hw_in);
DEFINE_FAKE_VALUE_FUNC(int64_t, hw_now_ms);
DEFINE_FAKE_VOID_FUNC(hw_sched_lock);
DEFINE_FAKE_VOID_FUNC(hw_sched_unlock);

int fake_in[LANE_COUNT][HW_IN_COUNT];
bool fake_out[LANE_COUNT][HW_OUT_COUNT];
int fake_out_on_count[LANE_COUNT][HW_OUT_COUNT];
int fake_out_err[LANE_COUNT][HW_OUT_COUNT];

/* Registra a escrita, a menos que o teste tenha pedido erro nessa saída. */
static int out_set_custom(enum lane_id lane, enum hw_out out, bool on)
{
	if (fake_out_err[lane][out] != 0) {
		return fake_out_err[lane][out];
	}
	fake_out[lane][out] = on;
	if (on) {
		fake_out_on_count[lane][out]++;
	}
	return 0;
}

static int in_get_custom(enum lane_id lane, enum hw_in in)
{
	return fake_in[lane][in];
}

void hw_fake_reset(void)
{
	RESET_FAKE(hw_init);
	RESET_FAKE(hw_out_set);
	RESET_FAKE(hw_in_get);
	RESET_FAKE(hw_now_ms);
	RESET_FAKE(hw_sched_lock);
	RESET_FAKE(hw_sched_unlock);
	FFF_RESET_HISTORY();

	memset(fake_in, 0, sizeof(fake_in));
	memset(fake_out, 0, sizeof(fake_out));
	memset(fake_out_on_count, 0, sizeof(fake_out_on_count));
	memset(fake_out_err, 0, sizeof(fake_out_err));
	for (int lane = 0; lane < LANE_COUNT; lane++) {
		fake_in[lane][HW_IN_LOWER] = 1;
	}
	hw_out_set_fake.custom_fake = out_set_custom;
	hw_in_get_fake.custom_fake = in_get_custom;
}
