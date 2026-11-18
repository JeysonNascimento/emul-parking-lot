/*
 * Máquina de estados de uma faixa (REQ-01, 09; o resto entra nas próximas
 * tarefas).
 */
#include "lane.h"

#include <zephyr/sys/util.h>

#include "adc_sampler.h"
#include "config.h"

void lane_init(struct lane *l, enum lane_id id)
{
	l->id = id;
	l->state = LANE_BOOT;
	l->buzzer_on = false;
	l->buzzer_off_ms = 0;
	barrier_init(&l->barrier, id);
}

/*
 * REQ-09: assistência. Motor parado, verde apagado, vermelho aceso e um
 * pulso de BUZZER_PULSE_MS. Se a faixa já está em assistência, não repete o
 * pulso; uma falha travada só promove ASSIST para LOCKED.
 * Erros de driver aqui são ignorados de propósito: não há o que fazer além
 * de tentar deixar tudo no estado seguro.
 */
static void assist(struct lane *l, bool latched, int64_t now_ms)
{
	bool already = l->state == LANE_ASSIST || l->state == LANE_LOCKED;

	if (l->barrier.state != BARRIER_FAULT) {
		barrier_fail(&l->barrier, BARRIER_FAULT_NONE);
	}
	(void)hw_out_set(l->id, HW_OUT_GREEN, false);
	(void)hw_out_set(l->id, HW_OUT_RED, true);
	if (!already) {
		(void)hw_out_set(l->id, HW_OUT_BUZZER, true);
		l->buzzer_on = true;
		l->buzzer_off_ms = now_ms + BUZZER_PULSE_MS;
	}
	if (latched || l->state == LANE_LOCKED) {
		l->state = LANE_LOCKED;
	} else {
		l->state = LANE_ASSIST;
	}
}

void lane_start(struct lane *l, int64_t now_ms)
{
	ARG_UNUSED(now_ms);
	(void)hw_out_set(l->id, HW_OUT_RED, true);
}

/* Conferência dos fins de curso, uma vez, depois da acomodação. */
static void boot_check(struct lane *l, int64_t now_ms)
{
	if (now_ms < STARTUP_SETTLE_MS) {
		return;
	}
	if (barrier_start(&l->barrier) == BARRIER_CLOSED) {
		l->state = LANE_IDLE;
	} else {
		/* Sem inferior, contraditórios ou erro: travado até o reset. */
		assist(l, true, now_ms);
	}
}

/* Desliga o buzzer quando o pulso termina. */
static void buzzer_service(struct lane *l, int64_t now_ms)
{
	if (l->buzzer_on && now_ms >= l->buzzer_off_ms) {
		(void)hw_out_set(l->id, HW_OUT_BUZZER, false);
		l->buzzer_on = false;
	}
}

void lane_step(struct lane *l, int64_t now_ms)
{
	enum barrier_state bs;

	if (l->state == LANE_BOOT) {
		boot_check(l, now_ms);
		return;
	}
	bs = barrier_step(&l->barrier, now_ms);
	buzzer_service(l, now_ms);

	if (l->state == LANE_LOCKED) {
		return;
	}
	if (bs == BARRIER_FAULT || adc_sampler_failed()) {
		/*
		 * Falha de cancela (posição ou leitura) ou do ADC: travada até o
		 * reset (REQ-09).
		 */
		assist(l, true, now_ms);
	}
}

void lane_on_input(struct lane *l)
{
	barrier_safety(&l->barrier);
}
