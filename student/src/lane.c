/*
 * Máquina de estados de uma faixa (REQ-01 a 04, 06 a 09). Ver lane.h.
 *
 * Ordem de um passo:
 *  1. cancela (prazos, fim de curso) e buzzer;
 *  2. falhas travadas (cancela, ADC);
 *  3. distância fresca e rearme;
 *  4. linhas recebidas da câmera;
 *  5. o estado atual decide.
 */
#include "lane.h"

#include <string.h>

#include <zephyr/sys/util.h>

#include "adc_sampler.h"
#include "config.h"
#include "sessions.h"

/* Câmera de cada faixa. */
static const enum hw_uart camera_uart[LANE_COUNT] = {
	[LANE_ENTRY] = HW_UART_CAMERA_ENTRY,
	[LANE_EXIT] = HW_UART_CAMERA_EXIT,
};

void lane_init(struct lane *l, enum lane_id id)
{
	memset(l, 0, sizeof(*l));
	l->id = id;
	l->state = LANE_BOOT;
	l->armed = true; /* um carro já parado na partida pode ser atendido */
	l->next_capture_id = 1;
	barrier_init(&l->barrier, id);
	lp_init(&l->cam_parser, true);
}

/*
 * REQ-09: assistência. Motor parado, verde apagado, vermelho aceso e um
 * pulso de BUZZER_PULSE_MS. Se a faixa já está em assistência, não repete o
 * pulso; uma falha travada só promove ASSIST para LOCKED.
 * Qualquer pedido pendente à câmera é invalidado.
 * Erros de driver aqui são ignorados de propósito: não há o que fazer além
 * de tentar deixar tudo no estado seguro.
 */
static void assist(struct lane *l, bool latched, int64_t now_ms)
{
	bool already = l->state == LANE_ASSIST || l->state == LANE_LOCKED;

	if (l->barrier.state != BARRIER_FAULT && l->barrier.state != BARRIER_CLOSED) {
		/* Cancela fora de 0°: falha de posição, sempre travada. */
		barrier_fail(&l->barrier, BARRIER_FAULT_NONE);
		latched = true;
	}
	(void)hw_out_set(l->id, HW_OUT_ENABLE, false);
	(void)hw_out_set(l->id, HW_OUT_GREEN, false);
	(void)hw_out_set(l->id, HW_OUT_RED, true);
	if (!already) {
		(void)hw_out_set(l->id, HW_OUT_BUZZER, true);
		l->buzzer_on = true;
		l->buzzer_off_ms = now_ms + BUZZER_PULSE_MS;
	}
	l->capture_id = 0;
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

/* REQ-02: dispara uma tentativa de captura com um ID novo. */
static void start_capture(struct lane *l, int64_t now_ms)
{
	char cmd[32];
	int n;

	l->capture_id = l->next_capture_id;
	l->next_capture_id = l->next_capture_id >= CAMERA_ID_MAX ? CAMERA_ID_MIN
								 : l->next_capture_id + 1;
	l->deadline_ms = now_ms + CAMERA_TIMEOUT_MS;
	l->state = LANE_IDENTIFY;

	n = camera_format_capture(cmd, sizeof(cmd), l->capture_id);
	if (n < 0 || hw_uart_write(camera_uart[l->id], cmd, (size_t)n) != 0) {
		assist(l, true, now_ms);
	}
}

/* Tentativa falhou (confiança baixa ou prazo): próxima ou assistência. */
static void next_attempt(struct lane *l, int64_t now_ms)
{
	if (l->attempt >= CAMERA_MAX_ATTEMPTS) {
		assist(l, false, now_ms);
		return;
	}
	l->attempt++;
	start_capture(l, now_ms);
}

/* Começa a abrir para a placa aceita. */
static void open_barrier(struct lane *l, int64_t now_ms)
{
	if (barrier_open(&l->barrier, now_ms) != 0) {
		assist(l, true, now_ms);
		return;
	}
	l->state = LANE_OPENING;
}

/* REQ-04: placa aceita; decide pelas sessões. */
static void plate_accepted(struct lane *l, const char *plate, int64_t now_ms)
{
	int64_t entry_ms;
	bool parked = sessions_find(plate, &entry_ms);

	strncpy(l->plate, plate, PLATE_LEN);
	l->plate[PLATE_LEN] = '\0';
	l->capture_id = 0; /* nenhuma outra resposta vale mais neste ciclo */

	if (l->id == LANE_ENTRY) {
		if (parked || !sessions_has_space()) {
			/* Já estacionada ou lotado: não abre. */
			assist(l, false, now_ms);
			return;
		}
		open_barrier(l, now_ms);
	} else {
		if (!parked) {
			/* Saída sem sessão (leitura errada): nunca cobra outro carro. */
			assist(l, false, now_ms);
			return;
		}
		/* Pagamento ainda não implementado: por ora, assistência. */
		assist(l, false, now_ms);
	}
}

/* Consome os bytes da câmera e trata as linhas completas. */
static void camera_service(struct lane *l, int64_t now_ms)
{
	uint8_t chunk[16];
	size_t n;

	while ((n = hw_uart_read(camera_uart[l->id], chunk, sizeof(chunk))) > 0) {
		for (size_t i = 0; i < n; i++) {
			struct camera_msg msg;

			if (lp_feed(&l->cam_parser, chunk[i], now_ms) != LP_LINE) {
				continue;
			}
			camera_parse(lp_line(&l->cam_parser), &msg);
			/* Só o +PLATE do pedido atual, enquanto identifica, conta. */
			if (l->state != LANE_IDENTIFY || msg.kind != CAMERA_PLATE ||
			    msg.id != l->capture_id) {
				continue;
			}
			if (msg.confidence >= CAMERA_MIN_CONFIDENCE) {
				plate_accepted(l, msg.plate, now_ms);
			} else {
				next_attempt(l, now_ms);
			}
		}
	}
}

/* REQ-07: passagem completa? */
static void passage_step(struct lane *l, int64_t now_ms)
{
	int beam = hw_in_get(l->id, HW_IN_BEAM);

	if (beam < 0) {
		assist(l, true, now_ms);
		return;
	}
	if (beam == 1) {
		l->beam_seen = true;
	}
	if (l->beam_seen && beam == 0 && l->dist_fresh && l->dist > DIST_ABSENT_ABOVE) {
		if (l->id == LANE_ENTRY) {
			/* A sessão só nasce com a passagem completa. */
			(void)sessions_add(l->plate, now_ms);
		} else {
			(void)sessions_remove(l->plate);
		}
		/* REQ-08: verde apagado e vermelho aceso antes de fechar. */
		(void)hw_out_set(l->id, HW_OUT_GREEN, false);
		(void)hw_out_set(l->id, HW_OUT_RED, true);
		if (barrier_close(&l->barrier, now_ms) != 0) {
			assist(l, true, now_ms);
			return;
		}
		l->state = LANE_CLOSING;
		return;
	}
	if (now_ms >= l->deadline_ms) {
		/* Cancela aberta sem passagem: travada, nunca fecha sozinha. */
		assist(l, true, now_ms);
	}
}

static void state_step(struct lane *l, int64_t now_ms)
{
	switch (l->state) {
	case LANE_IDLE:
		if (l->armed && l->dist_fresh && l->dist < DIST_PRESENT_BELOW) {
			l->armed = false;
			l->attempt = 1;
			start_capture(l, now_ms);
		}
		break;
	case LANE_IDENTIFY:
		if (l->dist_fresh && l->dist > DIST_ABSENT_ABOVE) {
			/* REQ-03: o carro foi embora; abandona e invalida o pedido. */
			l->capture_id = 0;
			l->state = LANE_IDLE;
		} else if (now_ms >= l->deadline_ms) {
			next_attempt(l, now_ms);
		}
		break;
	case LANE_OPENING:
		if (l->barrier.state == BARRIER_OPEN) {
			/* REQ-06: verde só com fim de curso superior e motor parado. */
			(void)hw_out_set(l->id, HW_OUT_RED, false);
			(void)hw_out_set(l->id, HW_OUT_GREEN, true);
			l->beam_seen = false;
			l->deadline_ms = now_ms + PASSAGE_TIMEOUT_MS;
			l->state = LANE_PASSAGE;
		}
		break;
	case LANE_PASSAGE:
		passage_step(l, now_ms);
		break;
	case LANE_CLOSING:
		if (l->barrier.state == BARRIER_CLOSED) {
			l->state = LANE_IDLE;
		}
		break;
	case LANE_ASSIST:
		/* REQ-09: rearme só com o carro longe e o feixe livre. */
		if (l->dist_fresh && l->dist > DIST_ABSENT_ABOVE &&
		    hw_in_get(l->id, HW_IN_BEAM) == 0) {
			l->state = LANE_IDLE;
		}
		break;
	default:
		break;
	}
}

void lane_step(struct lane *l, int64_t now_ms)
{
	enum barrier_state bs;
	uint16_t code;

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
		return;
	}

	l->dist_fresh = adc_sampler_take(l->id, &code);
	if (l->dist_fresh) {
		l->dist = code;
		if (code > DIST_ABSENT_ABOVE) {
			l->armed = true; /* o carro anterior saiu */
		}
	}

	camera_service(l, now_ms);
	state_step(l, now_ms);
}

void lane_on_input(struct lane *l)
{
	barrier_safety(&l->barrier);
}
