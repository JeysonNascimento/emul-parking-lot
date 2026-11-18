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

## Código de terceiros

Trechos copiados ou adaptados de outras fontes (documentação, exemplos do
Zephyr, fóruns), com origem e licença.

| Arquivo/trecho | Origem | Licença |
|---|---|---|
| | | |
