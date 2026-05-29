# Guia PIC2 — petBionics
**Afonso Cruz · 102815 · Entrega: 11 junho 2026**

---

## ⏰ Datas críticas

| Data | O quê |
|---|---|
| **Meados de maio 2026** | Júri proposto + PPT pronto ← **antes da SHELL** |
| **11 junho 2026** | Entrega PDF via Fénix (`102815-Afonso-Cruz.pdf`) |
| **3 julho 2026** | Apresentações/discussões (pedir data antes da SHELL) |

---

## FASE 1 — Ensaios do protótipo (fazer ANTES de escrever o Cap. 6)

Precisas de dados reais para o Cap. 6. Faz estes ensaios por ordem, guarda screenshots e CSV de cada um.

### Grupo A — Validação dos sensores (bancada, ~2h)

| # | Ensaio | O que mostrar | Figura para o relatório |
|---|---|---|---|
| A1 | **Boot completo** com todos os periféricos ligados | Serial: `IMU: OK`, `HX711: OK`, `SD: OK`, `WiFi: OK` | Screenshot do serial monitor |
| A2 | **Calibração da célula de carga** — colocar pesos conhecidos (0, 0.5, 1, 2 kg) | Valor estimado em kg vs. peso real → gráfico de linearidade | Gráfico kg_real vs kg_medido |
| A3 | **Validação do IMU — orientações estáticas** — flat, 90° roll, 90° pitch, vertical | roll/pitch/yaw corretos em cada posição | Tabela ou foto com ângulos medidos |
| A4 | **Validação do IMU — rotação lenta** — rodar devagar o dispositivo | yaw a mudar suavemente, roll/pitch estáveis | Plot de roll/pitch/yaw ao longo do tempo |

### Grupo B — Funcionalidades do sistema (bancada, ~1h)

| # | Ensaio | O que mostrar | Figura para o relatório |
|---|---|---|---|
| B1 | **Sessão completa via Web UI** — abrir browser, clicar Start, aguardar 30s, clicar Stop | Ficheiro CSV criado com timestamp correto, header completo | Screenshot da web interface |
| B2 | **Sessão completa via Serial** — `start`, aguardar 30s, `stop` | Output serial correto, contagem de amostras | Screenshot do serial monitor |
| B3 | **Interface serial — todos os comandos** — `help`, `status`, `files`, `wifi-off`, `wifi-on` | Todos respondem corretamente | Screenshot do serial (pode ser uma única foto longa) |
| B4 | **Download de ficheiro via Web UI** | Ficheiro CSV descarregado, abre no Excel/Python | — |
| B5 | **NTP time sync** — ligar com WiFi, verificar nome do ficheiro | Filename tem data/hora correta (não "unsynced") | — |
| B6 | **Modo low-power** — `sleep`, medir corrente, `wakeup` | Corrente cai (usar multímetro ou estimativa) | Opcional: foto do multímetro |

### Grupo C — Dados de movimento (o mais importante para o relatório, ~1h)

| # | Ensaio | O que mostrar | Figura para o relatório |
|---|---|---|---|
| C1 | **Impactos na célula de carga** — bater com o dedo ritmicamente | Picos no sinal raw, deteção de eventos (eventos++) | Plot: sinal + marcadores de evento |
| C2 | **Movimento com a mão** — segurar o dispositivo e caminhar | Acelerómetro XYZ com padrão de passo visível | Plot: ax, ay, az vs. tempo |
| C3 | **Orientação durante movimento** — rodar o dispositivo lentamente | Roll/pitch/yaw suaves, sem drift rápido | Plot: roll/pitch/yaw vs. tempo |
| C4 | **Sessão de marcha simulada** — amarrar o dispositivo ao pé/tornozelo e caminhar 30s | Sinal combinado: accel + load cell com padrão repetitivo | Plot: load cell + accel Z vs. tempo (melhor figura do relatório) |
| C5 | **Comparação raw vs. filtrado** — ver `load_cell_raw` vs. `load_cell_est_kg` no CSV | Filtro suaviza o sinal sem apagar eventos | Plot: raw vs. filtered sobrepostos |

> **Dica para C4:** Esta é a figura mais impactante do Cap. 6. Mesmo que seja no teu pé, mostra o conceito a funcionar.

---

## FASE 2 — Gráficos Python (depois dos ensaios, ~2h)

Para cada CSV do Grupo C, cria os gráficos com Python. Script base:

```python
import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv('sessao.csv')
fig, axes = plt.subplots(3, 1, figsize=(12, 8), sharex=True)

axes[0].plot(df['sample_us']/1e6, df['load_cell_est_kg'], label='Load Cell (kg)')
axes[0].set_ylabel('Force (kg)'); axes[0].legend()

axes[1].plot(df['sample_us']/1e6, df['imu_ax'], label='ax')
axes[1].plot(df['sample_us']/1e6, df['imu_ay'], label='ay')
axes[1].plot(df['sample_us']/1e6, df['imu_az'], label='az')
axes[1].set_ylabel('Acceleration (raw)'); axes[1].legend()

axes[2].plot(df['sample_us']/1e6, df['roll_deg'], label='Roll')
axes[2].plot(df['sample_us']/1e6, df['pitch_deg'], label='Pitch')
axes[2].plot(df['sample_us']/1e6, df['yaw_deg'], label='Yaw')
axes[2].set_ylabel('Orientation (°)'); axes[2].set_xlabel('Time (s)'); axes[2].legend()

plt.tight_layout()
plt.savefig('figura_preliminar.pdf')  # PDF para LaTeX
```

Guarda as figuras como **PDF** (melhor qualidade no LaTeX) em `05_figures/06_preliminary/`.

---

## FASE 3 — Pesquisa bibliográfica em falta (~2h)

### O que procurar (Google Scholar)

| O quê | Keywords | Onde colocar |
|---|---|---|
| Prótese canina instrumentada | `"instrumented prosthesis" dog` · `"sensorized" canine prosthetic` | Tabela Cap. 3 |
| Prótese humana instrumentada (se não houver canina) | `"instrumented prosthesis" force sensor gait` | Tabela Cap. 3 (com nota) |
| Anatomia membro canino | `canine limb anatomy locomotion biomechanics` | Cap. 2 nova secção |
| Ciclo de marcha canino (review) | `dog gait cycle review` · `canine locomotion biomechanics review` | Cap. 2 |
| Dados COVID adoção animais | `pet adoption COVID increase` · `AVMA pet ownership 2021` | Cap. 1 |

### Qualidade das fontes (critério do professor)
- Conferências: rank A ou B → [core.edu.au](https://core.edu.au/conference-ranks) ou [conferenceranks.org](http://www.conferenceranks.org)
- Journals: Q1 ou Q2 → [scimago.org](https://www.scimagojr.com)
- Preferir **surveys** e **reviews** da temática

---

## FASE 4 — Escrita LaTeX capítulo a capítulo

Atacar por esta ordem (do mais vazio ao mais completo):

### ① Cap. 6 — Preliminary Work ← **PRIORIDADE MÁXIMA**
**Conteúdo necessário:**
- [ ] Secção: descrição do hardware real (XIAO ESP32C3 + MPU-9250 + HX711 + LCR02 + SD + 3×LiPo)
- [ ] Secção: arquitectura do firmware (pipeline 80 Hz, orientação por quaterniões, deteção de eventos, CSV, web UI, interface série)
- [ ] Secção: resultados dos ensaios A1–A4 (validação sensores) com tabela/gráfico
- [ ] Secção: resultados dos ensaios C1–C5 (dados de movimento) com figuras Python
- [ ] Secção: limitações actuais (HX711 noise, calibração, placement IMU, autonomia)
- [ ] Figura: diagrama de blocos do hardware
- [ ] Figura: foto do protótipo (exterior + interior)
- [ ] Figura: screenshot web UI
- [ ] Figura: plot sinal de marcha simulada (C4)

### ② Cap. 7 — Work Plan ← **PRIORIDADE ALTA**
**Conteúdo necessário:**
- [ ] Gantt chart com 4 milestones (eu faço o código LaTeX com pgfgantt)
- [ ] Tabela de milestones com datas
- [ ] Secção de riscos e plano de contingência (3 riscos principais)

Milestones sugeridos:
1. ✅ M1 — Protótipo funcional + dataset preliminar (Mai 2026)
2. M2 — Campanha de recolha com animal real (Jul–Set 2026)
3. M3 — Implementação e avaliação modelos ML (Out–Nov 2026)
4. M4 — Integração, validação e escrita final (Dez 2026 – Jan 2027)

### ③ Cap. 8 — Conclusions ← **PRIORIDADE ALTA**
**Conteúdo necessário:**
- [ ] Resumo do que foi atingido (protótipo funcional, dados adquiridos, pipeline implementado)
- [ ] Resultados principais em 3–4 bullet points
- [ ] Trabalho futuro: recolha de dados com cão real, ML, validação clínica

### ④ Cap. 3 — State of the Art (tabela) ← **APÓS pesquisa bib**
**Conteúdo necessário:**
- [ ] Tabela com colunas: Paper | Instrumentada? | Robotizada? | Compósitos? | Nº cães validados
- [ ] ~12–15 entradas (já tens 10, precisas de 2–3 mais sobre próteses instrumentadas)
- [ ] Parágrafo a concluir que nenhum paper combina prótese + instrumentação → gap que o teu trabalho preenche

### ⑤ Cap. 2 — Background (anatomia canina) ← **APÓS pesquisa bib**
**Conteúdo necessário:**
- [ ] Nova secção: anatomia relevante do membro canino para próteses
- [ ] Nova secção (ou expandir existente): ciclo de marcha — fases stance/swing, parâmetros temporais típicos por raça
- [ ] Figura: diagrama do ciclo de marcha (de literatura com citação)

### ⑥ Cap. 1 — Introduction (adição pequena)
**Conteúdo necessário:**
- [ ] 1–2 frases sobre aumento de adoções durante COVID → crescimento do mercado de saúde animal → motivação adicional

### ⑦ Cap. 4 — Proposed Approach (preencher [To be added])
**Conteúdo necessário:**
- [ ] Análise de consumo: 100–175 mA médios (com WiFi), ~35–55 mA (sem WiFi), ~6–8h com 750 mAh
- [ ] Análise de armazenamento: 80 Hz × ~80 bytes/linha ≈ 6.4 KB/s ≈ 23 MB/h → SD de 8 GB suporta >340h

---

## FASE 5 — Figuras TikZ/LaTeX (eu faço estas)

Pede-me para gerar quando chegares a cada capítulo:

| Figura | Capítulo | Complexidade |
|---|---|---|
| Diagrama de blocos do sistema | Cap. 4 / Cap. 6 | TikZ — médio |
| Gantt chart com 4 milestones | Cap. 7 | pgfgantt — fácil |
| Diagrama ciclo de marcha canino | Cap. 2 | TikZ — médio |
| Gap analysis (tabela visual) | Cap. 3 | TikZ — fácil |

---

## Checklist regulamento PIC2

- [ ] Motivação e definição do problema → Cap. 1 ✅ (falta COVID)
- [ ] Enquadramento + revisão estado da arte → Cap. 2 ⚠️ + Cap. 3 ⚠️
- [ ] Definição e proposta de metodologias → Cap. 4 ✅ + Cap. 5 ✅
- [ ] Resultados esperados + preliminares → Cap. 6 ❌
- [ ] Planificação e calendarização → Cap. 7 ❌
- [ ] Nome do ficheiro: **`102815-Afonso-Cruz.pdf`**
- [ ] Submetido via Fénix → Projetos → "Relatório PIC2"

---

## Ordem recomendada de trabalho

```
Semana actual
├── Fase 1: Ensaios A1–A4 + B1–B3 (bancada, ~3h)
├── Fase 1: Ensaios C1–C5 (movimento, ~1h)
└── Fase 2: Gráficos Python dos dados (~2h)

Semana seguinte
├── Fase 3: Pesquisa bibliográfica (~2h)
├── Fase 4①: Escrever Cap. 6 com as figuras dos ensaios
└── Fase 4②: Escrever Cap. 7 com Gantt

Duas semanas antes da entrega
├── Fase 4③: Cap. 8 Conclusions
├── Fase 4④: Tabela estado da arte Cap. 3
├── Fase 4⑤⑥⑦: Adições aos caps 1, 2, 4
└── Revisão geral + compilar PDF + verificar nome do ficheiro
```
