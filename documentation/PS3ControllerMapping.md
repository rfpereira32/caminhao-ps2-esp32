# Caminhão RC — controle PS3

## Versão de referência

- Repositório: `rfpereira32/caminhao-ps2-esp32`
- Commit funcional: `2775b97` — `Versao com PS2`
- Controle testado: PS3 via Bluetooth
- Placa: ESP32 Dev Module
- Framework: Arduino / PlatformIO
- Estado: versão enviada à placa e testada no caminhão; todas as funções foram relatadas como funcionando normalmente.

O nome histórico do repositório e do commit menciona PS2, mas esta versão usa a biblioteca **PS3 Controller Host**. Esta documentação registra o comportamento do firmware nesse commit; não implica mudança de lógica ou de firmware.

## Arquitetura

O controle PS3 funciona como uma fonte de canais RC virtual. A leitura Bluetooth é copiada para `controlePS3`, convertida em valores de pulso em `pulseWidthRaw[]` e encaminhada para `processRawChannels()`. A lógica RC existente usa esses canais para controlar ESC, servos, luzes e sons.

```text
Controle PS3
    │ Bluetooth
    ▼
ESP32 / Ps3Controller
    ▼
controlePS3
    ▼
pulseWidthRaw[1..12]
    ▼
processRawChannels()
    ▼
Lógica RC original: ESC, direção/servos, luzes e sons
```

## Mapeamento dos canais

| Controle PS3 | Canal | Comportamento do sinal | Função associada |
|---|---:|---|---|
| Analógico direito, eixo X | CH1 | Proporcional: 1000–2000 µs; centro aproximado 1500 µs | Direção |
| — | CH2 | Fixo em 1500 µs | Câmbio; não é comandado pelo controle nesta versão |
| Analógico esquerdo, eixo Y | CH3 | Proporcional invertido: 2000–1000 µs | Acelerador / ré |
| X | CH4 | Momentâneo: pressionado 2000 µs, solto 1500 µs | Buzina e funções auxiliares, como luz azul/sirene conforme configuração |
| ↑ Cima | CH5 | Momentâneo: pressionado 2000 µs, solto 1500 µs | Funções auxiliares, como farol alto/baixo, neutro e Jake Brake conforme configuração |
| — | CH6 | Sem comando PS3 nesta versão | Não atribuído |
| — | CH7 | Sem comando PS3 nesta versão | Não atribuído |
| — | CH8 | Sem comando PS3 nesta versão | Não atribuído |
| ○ Círculo | CH9 | Toggle: alterna entre 1000 e 2000 µs | Farol de neblina |
| START | CH10 | Toggle: ligado 2000 µs; desligado 1500 µs | Liga/desliga motor |
| △ Triângulo | CH11 | Toggle: alterna entre 1000 e 2000 µs | Pisca-alerta |
| □ Quadrado | CH12 | Momentâneo: pressionado 2000 µs, solto 1000 µs | Lampejador do farol |

Os nomes das funções auxiliares dependem da configuração da lógica RC. CH6–CH8 são listados para completar o intervalo CH1–CH12, mas não recebem valores do controle PS3 no bloco ativo deste commit.

## Outros botões

| Controle PS3 | Comportamento observado no código |
|---|---|
| ← Esquerda / → Direita | Ajustes experimentais de `luzDeTeste`; não são atribuídos a CH1–CH12 |
| ↓ Baixo | Alterna uma variável interna (`BotaoBaixo`); não há saída ativa de canal associada nesse bloco |
| SELECT, botão PS, L1/R1/L2/R2/L3/R3 | Sem função efetiva no mapeamento ativo de canais |

## Referência no código

- Captura dos controles PS3: `notify()` em `src/src.cpp`.
- Conversão PS3 para CH1–CH12 e chamada de `processRawChannels()`: bloco de entrada PS3 em `src/src.cpp`.
- Biblioteca PS3 habilitada na configuração do projeto: `platformio.ini` e `src/0_generalSettings.h`.

Esta documentação descreve a versão existente. Qualquer mudança futura de comportamento deve ser feita separadamente e validada no caminhão.
