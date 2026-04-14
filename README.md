# 🚿🎵 Shower Music Controller

> A smart device that lets you control music in the shower using physical buttons and an ESP32, connected to a mobile app via Bluetooth.

## Overview
This project combines hardware and software to create a reliable, waterproof music controller. 
* **Hardware:** An ESP32 detects button presses and sends commands via Bluetooth Low Energy (BLE).
* **Software:** A connected mobile app receives these commands to control your music.
* **Environment:** Designed specifically for safe use in a wet environment.

## Features
* **Physical Controls:** Skip, volume adjustment, and play/pause functionality.
* **BLE Communication:** Rapidly sends mapped commands (e.g., "NEXT") to the paired device.
* **Mobile Interface:** Built using React Native (Expo).
* **Durability:** Custom waterproof enclosure design.

## How It Works
1. Press the Bluetooth connect button to pair the device.
2. The ESP32 detects physical button inputs.
3. The ESP32 sends the corresponding BLE command.
4. The mobile app receives the command and adjusts the music playback.

## Structure
shower-controller/
├── arduino/       # ESP32 microcontroller code
├── app/           # React Native mobile app code


## 🚀 Status
**Work in Progress:** Currently adding more features and improving overall system reliability.
