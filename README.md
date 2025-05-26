# Projeto Base ESP RainMaker

Este projeto serve como template para novos dispositivos ESP RainMaker com botão físico (GPIO4) e LED externo (GPIO5).

## Funcionalidades
- Controle de relé/saída via botão físico (GPIO4)
- LED indicador externo (GPIO5)
- Não realiza mais reset Wi-Fi/fábrica via botão
- Código em domínio público (Public Domain/CC0)

## Como usar
Siga a documentação oficial do ESP RainMaker para compilar, flashar e provisionar o dispositivo: [Get Started](https://rainmaker.espressif.com/docs/get-started.html)

## Observações
- O botão físico apenas alterna o estado do relé/saída e do LED externo.
- O reset de fábrica/provisionamento Wi-Fi não está mais vinculado ao botão.
- Ideal para servir de base para projetos customizados ESP RainMaker.
