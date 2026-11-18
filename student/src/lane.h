/*
 * Máquina de estados de uma faixa. As duas faixas usam o mesmo código;
 * cada uma tem a sua struct lane.
 */
#ifndef PARKING_LANE_H
#define PARKING_LANE_H

#include <stdbool.h>
#include <stdint.h>

#include "barrier.h"
#include "hw.h"

enum lane_state {
	LANE_BOOT = 0, /* partida: motor desligado, esperando as entradas */
	LANE_IDLE,     /* cancela fechada, esperando carro */
	LANE_ASSIST,   /* assistência rearmável (cancela fechada) */
	LANE_LOCKED,   /* assistência travada até o reset */
};

struct lane {
	enum lane_id id;
	enum lane_state state;
	struct barrier barrier;
	bool buzzer_on;
	int64_t buzzer_off_ms; /* quando desligar o pulso do buzzer */
};

/** Zera a faixa. Não acessa o hardware. */
void lane_init(struct lane *l, enum lane_id id);

/**
 * Partida segura (REQ-01): acende o vermelho. Os fins de curso são
 * conferidos no primeiro passo depois de STARTUP_SETTLE_MS.
 */
void lane_start(struct lane *l, int64_t now_ms);

/** Um passo do laço principal (a cada TICK_MS). */
void lane_step(struct lane *l, int64_t now_ms);

/** Chamado pela work quando uma entrada da faixa muda (parada urgente). */
void lane_on_input(struct lane *l);

#endif /* PARKING_LANE_H */
