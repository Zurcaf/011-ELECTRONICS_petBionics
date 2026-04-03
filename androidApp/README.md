# petBionic Android App

Aplicacao Android para controlo BLE do firmware petBionic e consulta de estado/sessoes.

## Objetivo

- Ligar ao ESP32-C3 via BLE
- Enviar comandos de controlo da aquisicao
- Mostrar estado operacional em tempo real
- Consultar historico e detalhe de sessoes

## Estrutura principal

- `app/src/main/java/com/example/petbionic/MainActivity.kt`
  - Ecra principal
  - Estado da sessao/dispositivo
  - Comandos START/STOP

- `app/src/main/java/com/example/petbionic/BleManager.kt`
  - Scan e conexao BLE
  - Escrita de comandos
  - Rececao de notificacoes de estado

- `app/src/main/java/com/example/petbionic/HistoryActivity.kt`
  - Lista de sessoes

- `app/src/main/java/com/example/petbionic/SessionDetailActivity.kt`
  - Detalhe de uma sessao

## Contrato BLE (resumo)

- Comandos comuns: `START`, `STOP`, `TIME=<epoch_ms>`, `PERIOD=<ms>`, `RATE=<hz>`
- Estado recebido: JSON com campos de aquisicao/saude/sincronizacao

Para UUIDs e detalhes completos do protocolo, ver:
- `firmware/platformio_petBionics/README.md`

## Build e execucao

Na pasta `androidApp`:

```bash
./gradlew assembleDebug
```

Para correr testes unitarios (quando existirem):

```bash
./gradlew test
```

## Notas operacionais

- A app tenta reenviar TIME quando o firmware sinaliza `time_sync_needed`.
- O ecrã principal mostra estado de SD, IMU, HX711 e samples.
