# Projeto Base ESP RainMaker - Switch com Relé, LED Externo e Monitoramento de Bateria

Este projeto é um template para dispositivos ESP RainMaker com controle de relé via botão físico, LED externo, monitoramento de tensão de baterias 18650 e modos automáticos de economia de energia.

## Funcionalidades
- Controle de relé/saída via botão físico (GPIO4)
- LED indicador externo (GPIO5) acompanha o estado do relé
- Reset Wi-Fi (3s) e reset de fábrica (10s) via botão no GPIO0
- Monitoramento da tensão do banco de baterias (Baterias 18650) via ADC (GPIO34)
- Parâmetro de tensão exibido na app ESP RainMaker, com 2 casas decimais
- Modos automáticos de economia de energia:
  - **Light Sleep**: das 18:30 às 19:59 se tensão > 3.5V
  - **Hibernação (Deep Sleep)**: das 20:00 às 5:59 ou se tensão < 3.1V
  - Retoma operação normal após 6:00 ou se tensão subir para 3.7V

## Como usar
Siga a documentação oficial do ESP RainMaker para compilar, flashar e provisionar o dispositivo: [Get Started](https://rainmaker.espressif.com/docs/get-started.html)

### Resumo dos GPIOs
- **GPIO4**: Botão físico para alternar o relé/LED
- **GPIO5**: LED externo (indica o estado do relé)
- **GPIO0**: Botão de reset Wi-Fi/fábrica (pressione 3s para reset Wi-Fi, 10s para reset de fábrica)
- **GPIO34**: Leitura da tensão do banco de baterias (divisor resistivo 49.2kΩ + 49.2kΩ)
- **OUTPUT_GPIO**: GPIO configurável para o relé (definido em `sdkconfig`)

## O que esperar deste exemplo?
- Pressionar o botão físico (GPIO4) alterna o relé e o LED externo.
- O estado do relé/LED pode ser controlado também pelo app ESP RainMaker.
- O parâmetro "Baterias 18650" mostra a tensão do banco de baterias na app.
- O ESP32 entra automaticamente em light sleep ou hibernação conforme horário e tensão das baterias.

## Observações
- Durante hibernação (deep sleep), o dispositivo não responde ao app RainMaker.
- O botão físico (GPIO4) não faz reset Wi-Fi/fábrica.
- Ideal para servir de base para projetos customizados ESP RainMaker com foco em autonomia e proteção de baterias.

## Exemplo de monitoramento
Após flashar, para monitorar os logs no Windows (ajuste a porta se necessário):

```powershell
idf.py -p COM5 monitor
```

## Licença
Domínio público (Public Domain/CC0).
