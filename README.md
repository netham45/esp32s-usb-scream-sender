# ESP32S3 USB Scream Sender

This implements a USB UAC 1.0 Sound Card that can be used to forward audio from any device that accepts USB sound cards (PC, PS5, Switch, phones) to ScreamRouter

This requires an ESP32S3 for USB Device support.

[Repo Link](https://github.com/netham45/esp32s-usb-scream-sender)

## Building

Copy main/secrets_example.h to main/secrets.h and fill out the file
Build and Flash with ESP-IDF: idf-py -p <esp32s3 com port> flash monitor
