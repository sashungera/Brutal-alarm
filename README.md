# Brutal Alarm

A brutal alarm clock built with an ESP32 that is designed to make waking up impossible to ignore.

Unlike a regular alarm, **Brutal Alarm cannot be turned off immediately**. Once it starts, the sound volume and LED brightness gradually increase over one minute while the stop button remains locked. Only after the lock period expires can the alarm be dismissed.

---

## Features

* Real-Time Clock (DS3231)
* OLED display for time and alarm information
* Three-button interface
* Gradually increasing alarm volume
* Gradually increasing LED brightness
* 60-second anti-snooze lock
* Adjustable alarm time
* State-machine based software architecture
* Debounced button handling
* I2S audio output using MAX98357A amplifier

---

## Hardware

* ESP32 (Lolin32 Lite)
* DS3231 RTC Module
* SSD1306 OLED Display (128×64)
* MAX98357A I2S Audio Amplifier
* Speaker
* LED
* 3 Push Buttons

---

## Software

The project is written in **C++** using the **Arduino framework**.

### Libraries

* Adafruit SSD1306
* Adafruit GFX
* RTClib
* ESP32 Arduino Core

---

## How It Works

The firmware is organized as a finite state machine consisting of three states:

### Normal Mode

* Displays the current time.
* Continuously checks the RTC for the alarm time.
* Allows entering the alarm setup menu.

### Alarm Setup

The user can:

* Change the alarm hour
* Change the alarm minute
* Save the new alarm time

### Alarm Mode

When the alarm triggers:

* The speaker starts quietly.
* The volume gradually increases over 60 seconds.
* The LED brightness increases simultaneously.
* The alarm **cannot be stopped** during the first 60 seconds.
* After the lock period expires, pressing the **MODE** button stops the alarm.

---

## State Diagram

```text
           +-----------+
           |  NORMAL   |
           +-----------+
                 |
            MODE Button
                 |
                 v
          +-------------+
          |   SETTING   |
          +-------------+
                 |
            Save Alarm
                 |
                 v
           +-----------+
           |  NORMAL   |
           +-----------+
                 |
          Alarm Time Reached
                 |
                 v
          +-------------+
          |    ALARM    |
          +-------------+
                 |
        60 s Lock Expires
                 |
          MODE Button
                 |
                 v
           +-----------+
           |  NORMAL   |
           +-----------+
```

---

## Repository Structure

```text
Brutal-Alarm/
│
├── BrutalAlarm.ino
├── README.md
└── Documentation/
```

---

## Documentation

A complete project documentation, including:

* Hardware wiring
* Circuit diagrams
* Pin configuration
* Software architecture
* Code explanation
* Photos
* Design decisions

is available in the **Documentation** folder.

---

## Author

Created by **Sashungera Sera** as an embedded systems project using ESP32 and Arduino.
