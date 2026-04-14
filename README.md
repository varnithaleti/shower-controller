🚿🎵 Shower Music Controller
   A smart device that lets you control music in the shower using physical buttons and an ESP32, connected to a mobile app via Bluetooth.

Overview
This project combines hardware + software to create a waterproof music controller.
   - ESP32 sends commands using Bluetooth (BLE)
   - Mobile app receives commands and controls music
   - Designed for use in a wet environment

Features
   - Button controls: skip, volume, play/pause
   - BLE communication (sends commands like "NEXT")
   - React Native (Expo) mobile app
   - Waterproof enclosure design
  
Structure
   shower-controller/
   ├── arduino/    # ESP32 code
   ├── app/        # Mobile app

How it works
   - Press connect bluetooth button
   - ESP32 detects input
   - Sends BLE command
   - App receives it and controls music

Status
- Work in progress & adding more features and improving reliability.
