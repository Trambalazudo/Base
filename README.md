# Projeto Base ESP RainMaker - Switch com Relé, LED Externo e Monitoramento de Bateria

Este projeto é um template para dispositivos ESP RainMaker com controle de relé via botão físico, LED externo, monitoramento de tensão de baterias 18650 e modos automáticos de economia de energia.

## Funcionalidades
- Controle de relé/saída via botão físico (GPIO4)
- LED indicador externo (GPIO5) acompanha o estado do relé e pode ser controlado tanto pelo botão físico quanto pelo app ESP RainMaker (sincronização total)
- Reset Wi-Fi (3s) e reset de fábrica (10s) via botão no GPIO0
- Monitoramento da tensão do banco de baterias (Baterias 18650) via ADC (GPIO34)
- Parâmetro de tensão exibido na app ESP RainMaker, com 2 casas decimais
- Exibe também a percentagem da bateria (parâmetro "Bateria (%)"), calculada de forma linear entre 3.20V (0%) e 4.20V (100%).
- Modos automáticos de economia de energia:
  - **Light Sleep**: das 18:30 às 19:59 se tensão > 3.5V
  - **Hibernação (Deep Sleep)**: das 20:00 às 5:59 ou se tensão < 3.1V
  - Retoma operação normal após 6:00 ou se tensão subir para 3.7V

## Como usar
Siga a documentação oficial do ESP RainMaker para compilar, flashar e provisionar o dispositivo: [Get Started](https://rainmaker.espressif.com/docs/get-started.html)

### Resumo dos GPIOs
- **GPIO4**: Botão físico para alternar o LED
- **GPIO5**: LED externo (indica o estado do LED)
- **GPIO0**: Botão de reset Wi-Fi/fábrica (pressione 3s para reset Wi-Fi, 10s para reset de fábrica)
- **GPIO34**: Leitura da tensão do banco de baterias (divisor resistivo 49.2kΩ + 49.2kΩ)
- **OUTPUT_GPIO**: GPIO configurável para o relé (definido em `sdkconfig`)

## O que esperar deste exemplo?
- Pressionar o botão físico (GPIO4) alterna o LED externo.
- O estado do LED pode ser controlado também pelo botão físico (GPIO4) e pelo app ESP RainMaker, e ambos ficam sempre sincronizados.
- O parâmetro "Baterias 18650" mostra a tensão do banco de baterias na app.
- O parâmetro "Bateria (%)" exibe a percentagem da bateria, calculada linearmente com base na tensão.
- O ESP32 entra automaticamente em light sleep ou hibernação conforme horário e tensão das baterias.
- A medição manual ("Medir Bateria") não permite múltiplas execuções simultâneas, mesmo com comandos repetidos do app.

## Observações
- Durante hibernação (deep sleep), o dispositivo não responde ao app RainMaker.
- O botão físico (GPIO4) não faz reset Wi-Fi/fábrica.
- O LED pode ser controlado tanto pelo botão físico quanto pelo app, e o estado é sempre refletido em ambos.
- A rotina de medição manual de bateria é protegida contra reentrância: só uma medição ocorre por vez.
- Ideal para servir de base para projetos customizados ESP RainMaker com foco em autonomia e proteção de baterias.

## Exemplo de monitoramento
Após flashar, para monitorar os logs no Windows (ajuste a porta se necessário):

```powershell
idf.py -p COM5 monitor
```

## Estados do parâmetro "Status Bateria"
O parâmetro **Status Bateria** indica o estado do banco de baterias conforme a tensão medida. Os estados possíveis são:

| Tensão (V)         | Estado informado           |
|--------------------|---------------------------|
| menor que 3.20     | Entrar em hibernação      |
| 3.20 a 3.29        | Preparar hibernação       |
| 3.30 a 3.49        | Redução de consumo        |
| 3.50 ou maior      | OK                        |

Esses estados são atualizados automaticamente no app ESP RainMaker sempre que a tensão entra em uma nova faixa.

## Licença
Domínio público (Public Domain/CC0).
