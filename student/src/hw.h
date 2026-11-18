/*
 * Camada fina de hardware: a única parte do firmware que chama os drivers
 * do Zephyr. A lógica (cancela, faixa, protocolos) só usa estas funções.
 * Nos testes de unidade elas são substituídas por falsos do FFF.
 */
#ifndef PARKING_HW_H
#define PARKING_HW_H

#include <stdbool.h>
#include <stdint.h>

/** Faixas do estacionamento. Servem de índice nos vetores. */
enum lane_id {
	LANE_ENTRY = 0,
	LANE_EXIT = 1,
	LANE_COUNT
};

/** Saídas digitais de uma faixa (todas ativas em nível alto). */
enum hw_out {
	HW_OUT_ENABLE = 0, /* motor gira */
	HW_OUT_DIRECTION,  /* 1 abre, 0 fecha */
	HW_OUT_RED,
	HW_OUT_GREEN,
	HW_OUT_BUZZER,
	HW_OUT_COUNT
};

/** Entradas digitais de uma faixa (ativas em nível alto, com EXTI). */
enum hw_in {
	HW_IN_UPPER = 0, /* fim de curso superior: braço em 90° */
	HW_IN_LOWER,     /* fim de curso inferior: braço em 0° */
	HW_IN_BEAM,      /* feixe de passagem: 1 = algo no feixe */
	HW_IN_COUNT
};

/**
 * Função chamada em contexto de work (fila do sistema) sempre que uma
 * entrada da faixa muda. É ali que o motor é parado com urgência.
 */
typedef void (*hw_input_handler_t)(enum lane_id lane);

/**
 * Configura saídas (inativas), entradas e interrupções.
 * @return 0 ou -errno se algum dispositivo não estiver pronto.
 */
int hw_init(hw_input_handler_t handler);

/** Escreve uma saída. @return 0 ou -errno do driver. */
int hw_out_set(enum lane_id lane, enum hw_out out, bool on);

/** Lê uma entrada. @return 0, 1 ou -errno do driver. */
int hw_in_get(enum lane_id lane, enum hw_in in);

/** Tempo virtual desde o boot, em ms. */
int64_t hw_now_ms(void);

/*
 * Impedem a troca de thread (a work de parada não roda) entre as duas
 * chamadas. Usado só para "marcar o movimento e ligar o motor" juntos.
 */
void hw_sched_lock(void);
void hw_sched_unlock(void);

#endif /* PARKING_HW_H */
