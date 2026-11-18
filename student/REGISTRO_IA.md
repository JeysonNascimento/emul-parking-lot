# Registro de uso de IA

Preencha durante o trabalho, não no fim. É o portão G4 da avaliação
([desafio](../docs/desafio.md#portões)): os dois alunos da dupla precisam ter
entradas. Quem não usou IA escreve isso aqui.

## Dupla

Trabalho individual.

| Aluno | Matrícula | Partes que escreveu |
|---|---|---|
| Jeyson Nascimento | (preencher) | todo o `student/` |

## Ferramentas

| Aluno | Ferramenta | Modelo/versão | Período de uso |
|---|---|---|---|
| Jeyson Nascimento | Claude Code (CLI) | Claude Opus 5.5 | 2026-11-18 em diante |

## Tarefas

Uma entrada por tarefa relevante (um requisito, um bug, uma refatoração).

### 2026-11-18 — Análise do desafio, especificação e plano

- **Aluno:** Jeyson Nascimento
- **Requisito(s):** todos (REQ-01 a REQ-13)
- **O que pedi:** análise do `desafio.pdf`, uma especificação, a explicação
  de cada requisito para a sabatina e um plano de tarefas.
- **O que a IA propôs:** especificação com módulos (`hw`, `barrier`, `lane`,
  `adc_sampler`, `line_parser`, `camera`, `payment`, `tariff`, `sessions`,
  `display`), laço único de 10 ms com parada do motor via EXTI → `k_work`,
  UART por interrupção; explicação dos requisitos; plano em 18 tarefas.
- **O que aceitei, mudei ou rejeitei, e por quê:** escolhi o laço único em
  vez de uma thread por faixa (sem mutex nos dados compartilhados, mais fácil
  de testar e explicar) e a UART por interrupção.
- **Como verifiquei:** leitura do enunciado e dos contratos em `docs/`.
- **O que aprendi / o que ainda preciso entender:**

### 2026-11-18 — Instalação do laboratório

- **Aluno:** Jeyson Nascimento
- **Requisito(s):** —
- **O que pedi:** instalar o ambiente e ligá-lo ao repositório privado.
- **O que a IA propôs:** seguiu `docs/instalacao.md`. Como o `scripts/lab.py`
  recusava o Renode 1.17.0 oficial (SHA-256 conferido; o binário reporta o
  build `20260907` e o script espera `20260906`), propôs um ajuste **só
  local** dessa checagem, fora dos commits (`git update-index --skip-worktree`).
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei o ajuste local e
  vou avisar o professor. Nada fora de `student/` é entregue.
- **Como verifiquei:** cenário `normal` falhou exatamente como o guia prevê
  (`position_mdeg expected 90000, observed 0`).
- **O que aprendi / o que ainda preciso entender:**

### 2026-11-18 — Camada de hardware, cancela e partida segura

- **Aluno:** Jeyson Nascimento
- **Requisito(s):** REQ-01, REQ-06, REQ-08, REQ-09 (parte)
- **O que pedi:** a tarefa T2 do plano.
- **O que a IA propôs:** `src/config.h` (constantes), `src/hw.c/.h` (GPIO,
  EXTI → `k_work`), `src/barrier.c/.h` (estados da cancela, prazo de 2500 ms,
  parada pelo fim de curso, obstrução, contraditórios; comunicação com a work
  só por `atomic_t` e `k_sched_lock` ao ligar o motor), `src/lane.c/.h`
  (partida e assistência com um único pulso de buzzer), `src/main.c` (laço de
  10 ms); suítes `tests/barrier` e `tests/lane` com `tests/common/hw_fake.c`.
  Removeu o exemplo `tests/unit`.
- **O que aceitei, mudei ou rejeitei, e por quê:** (preencher após revisar)
- **Como verifiquei:** cenários `partial-startup` e `contradictory-limits`
  passam; `firmware-coverage.py` com as suítes `unit:barrier` e `unit:lane`
  passando (parcial: 95,2% linhas, 85,3% branches).
- **O que aprendi / o que ainda preciso entender:**

### 2026-11-18 — ADC4 assíncrono e acomodação da partida

- **Aluno:** Jeyson Nascimento
- **Requisito(s):** REQ-10, REQ-01
- **O que pedi:** a tarefa T5 do plano.
- **O que a IA propôs:** `hw_adc_start`/`hw_adc_poll` em `src/hw.c`, com
  buffer, `adc_sequence` e `k_poll_signal` estáticos, verificação por
  `k_poll_signal_check`; `src/adc_sampler.c` com um pedido por vez,
  alternância dos canais 17/18, amostra fresca consumida uma vez, prazo de
  100 ms e falha travada sem reenvio; falha do ADC leva as faixas a
  assistência travada. Suítes `tests/adc` (falsos) e `tests/hw` (drivers
  reais contra os modelos do Renode).
  Achado durante a verificação: logo após o boot (t = 278 µs) as entradas
  ainda não refletem os modelos, e a faixa de saída pedia assistência sem
  motivo. A conferência dos fins de curso passou para o primeiro passo depois
  de `STARTUP_SETTLE_MS` (20 ms), com motor desligado e vermelho aceso.
- **O que aceitei, mudei ou rejeitei, e por quê:** (preencher após revisar)
- **Como verifiquei:** eventos do cenário `partial-startup`: buzzer da saída
  acionado em t = 278 µs antes da correção e nenhuma vez depois;
  `partial-startup` e `contradictory-limits` passam; suítes `unit:adc`,
  `unit:barrier`, `unit:hw` e `unit:lane` passam (98,9% linhas, 88,2%
  branches no parcial). Na suíte `hw`, o ADC real lê ~4095 nos dois canais.
- **O que aprendi / o que ainda preciso entender:**

### 2026-11-18 — Montador de linhas, protocolo da câmera e sessões

- **Aluno:** Jeyson Nascimento
- **Requisito(s):** REQ-11, REQ-02 (sintaxe), REQ-04 (tabela)
- **O que pedi:** as tarefas T7, T8 (parte de protocolo) e T9 do plano.
- **O que a IA propôs:** `src/line_parser.c` (quadro de 128 bytes com o LF,
  modo CRLF ou só LF, ASCII imprimível, prazo de montagem de 1000 ms,
  descarte até o LF, `lp_parse_uint` para decimais canônicos);
  `src/camera.c` (`AT+CAPTURE`, `OK`, `ERROR`, `+PLATE` estrito, expressão
  da placa); `src/sessions.c` (tabela por placa com capacidade do Kconfig).
  Suíte `tests/proto`.
- **O que aceitei, mudei ou rejeitei, e por quê:** (preencher após revisar)
- **Como verifiquei:** suíte `unit:proto` passa (um teste errado de contagem
  de bytes foi corrigido: `AT+CAPTURE=999999999\r\n` tem 22 bytes).
- **O que aprendi / o que ainda preciso entender:**

### 2026-11-18 — UART por interrupção e ciclo completo da entrada

- **Aluno:** Jeyson Nascimento
- **Requisito(s):** REQ-02, 03, 04, 06, 07, 08, 09, 11
- **O que pedi:** as tarefas T6, T8 (UART) e T10 do plano.
- **O que a IA propôs:** recepção por interrupção em `src/hw.c` (ISR esvazia
  a FIFO num `ring_buf` por UART, um produtor e um consumidor, sem trava) e
  transmissão por `uart_poll_out`; `src/lane.c` com os estados IDLE,
  IDENTIFY, OPENING, PASSAGE, CLOSING, ASSIST e LOCKED: 3 tentativas com ID
  novo e 1000 ms, histerese 683/1024 com a flag `armed`, abandono ao se
  afastar, decisão por sessões, verde só com a cancela aberta e o motor
  parado, passagem por feixe ativo→inativo e distância > 1024, sessão criada
  só na passagem, fechamento com verde apagado, rearme da assistência com o
  carro longe e o feixe livre. Suíte `tests/lane` reescrita (falsos de UART
  com injeção de bytes).
- **O que aceitei, mudei ou rejeitei, e por quê:** (preencher após revisar)
- **Como verifiquei:** passam `confidence-retry`, `low-confidence`,
  `camera-timeout`, `stationary`, `noisy-threshold`, `jam`, `missing-upper`,
  `missing-lower`, `obstruction-before-close`, `obstruction-during-close`,
  `partial-startup` e `contradictory-limits` (os 4 do portão G3 incluídos).
  `normal` falha na saída (pagamento ainda não implementado), como esperado.
  Todas as suítes passam.
- **O que aprendi / o que ainda preciso entender:**

## Código de terceiros

Trechos copiados ou adaptados de outras fontes (documentação, exemplos do
Zephyr, fóruns), com origem e licença.

| Arquivo/trecho | Origem | Licença |
|---|---|---|
| | | |
