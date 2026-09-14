# ESP32-Based Smart Seedling Watering System with Automated Environmental Control

A low-cost, locally controlled seedling watering prototype designed to reduce repetitive manual watering while combining embedded programming, environmental sensing, real-time scheduling, relay/solenoid control, fluid delivery, and mechanical fabrication.

> **Project:** Midterm Project #1  
> **Course:** Microprocessors, Microcontroller Systems and Design  
> **Author:** Rovic B. Magdamit  
> **Institution:** University of Caloocan City – College of Engineering

## Overview

The system automates watering for a small seedling tray using an **ESP32-C3** as the central controller. It uses a **DHT22/AM2302** for temperature and relative-humidity monitoring and a **DS3231 RTC** for the time reference required by daily watering schedules.

A phone or laptop connects directly to the ESP32's local Wi-Fi access point and opens the controller's web interface. No cloud service or external router is required.

The final prototype uses a gravity-fed water path:

```text
~10-L Reservoir
      │
      ▼
 Clear Tubing
      │
      ▼
 Solenoid Valve
      │
      ▼
 Water Outlet
      │
      ▼
 Seedling Tray
```

The electrical/control path is:

```text
DHT22 ───────────────┐
                     │
DS3231 ──────────────┤
                     ▼
                ESP32-C3
                     │
              Control Logic
                     │
                     ▼
              Transistor/Relay
                     │
                     ▼
               Solenoid Valve
```

## Main Features

- Daily scheduled watering using the DS3231 RTC
- Configurable temperature and relative-humidity rules
- Optional second environmental condition using **AND** or **OR**
- Configurable persistence period before an environmental rule can trigger watering
- Cooldown period to reduce repeated environmental triggering
- Wireless manual watering
- Wireless force-stop watering
- Hard software watering limit of **30 minutes**
- Non-blocking `millis()` timing for watering duration, sensor polling, persistence, and cooldown
- Local Wi-Fi SoftAP + HTTP web interface
- Configuration stored in ESP32 non-volatile storage using `Preferences`
- Connected devices can retrieve the ESP32's stored schedules and environmental rules
- DS3231 retains the time reference when the ESP32 is unpowered
- Relay and solenoid inductive-load protection using flyback diodes

## Hardware

### Controller

| Component | Function |
|---|---|
| ESP32-C3 SuperMini | Main controller, Wi-Fi access point, web server |
| DHT22 / AM2302 | Temperature and relative-humidity sensing |
| DS3231 RTC | Time-of-day reference for scheduled watering |
| SRD-05VDC-SL-C relay | Electrical isolation/switching for watering actuator |
| PN2222A transistor drivers | Relay/actuator driver stage |
| LM7805 | 5 V regulation |
| Flyback diodes | Inductive-load protection |
| LED indicator | Visual status indication |
| DC barrel jack | External power input |
| Terminal blocks / headers | Wiring connections |

### Watering and Structure

- Approximately 10-L plastic reservoir
- Clear tubing
- DC solenoid valve
- Seedling tray
- Blue PVC pipe
- PVC elbow and tee fittings
- Nylon rope for reservoir/outlet support
- Clear acrylic electronics enclosure
- Acrylic adhesive

## ESP32-C3 Pin Assignment

| ESP32-C3 Pin | Assignment | Function |
|---|---|---|
| GPIO4 | `DHT_PIN` | DHT22 / AM2302 data |
| GPIO5 | `WATER_PIN` | Relay/transistor watering control |
| GPIO6 | `RTC_SDA` | DS3231 SDA |
| GPIO7 | `RTC_SCL` | DS3231 SCL |
| I2C `0x68` | DS3231 | RTC device address |
| `192.168.4.1` | Wi-Fi AP | Local HTTP configuration interface |

## Power Arrangement

The prototype uses a **12 V DC input** before regulation.

The controller uses a regulated **5 V rail** for the low-voltage electronics and compatible actuator/control circuitry.

The solenoid is controlled through the relay/transistor stage rather than directly from an ESP32 GPIO.

> The external adapter's current rating is a maximum available capacity; the connected load draws the current required by the circuit.

## Software Architecture

The firmware runs as a continuously serviced control loop.

The major tasks are:

1. Service the local HTTP server.
2. Read the DHT22 at the configured sensor interval.
3. Read the DS3231 for calendar/time-of-day information.
4. Evaluate scheduled watering events.
5. Evaluate environmental rules.
6. Control the relay/solenoid watering output.
7. Enforce watering duration and safety limits.
8. Save/retrieve controller configuration through non-volatile storage.

### Non-Blocking Timing

Watering is intentionally not implemented as a long `delay()` operation.

When watering begins, the firmware records the starting value of `millis()` and checks elapsed time on later loop iterations.

The same approach is used for:

- watering duration
- environmental persistence
- cooldown
- sensor polling

This allows the ESP32 to continue servicing the web interface and other controller tasks while watering is active.

## Scheduling and Environmental Rules

### Scheduled Watering

A scheduled event contains:

- time of day
- watering duration
- enable state

The DS3231 provides the calendar-time reference used to determine when a daily event is due.

### Environmental Watering

An environmental rule can use:

- temperature or relative humidity
- comparison operator
- threshold
- optional second condition
- `AND` / `OR` logic
- persistence period
- watering duration
- cooldown

For example, a rule can conceptually be configured as:

```text
IF temperature > threshold
AND the condition remains true for the persistence period
THEN water for the configured duration
AND wait for the cooldown before another trigger
```

The system does **not** claim that one watering amount is correct for every plant. Watering duration is configurable because plant type, tray geometry, substrate, age, temperature, and humidity can change actual water requirements.

## Local Web Interface

The ESP32 creates its own local Wi-Fi network:

```text
SSID: ESP32_Smart_Seedling_Waterer
IP:   192.168.4.1
```

A connected phone or laptop can use the browser interface for:

- schedule configuration
- temperature/humidity rule configuration
- automation control
- manual watering
- force stop
- controller status monitoring
- retrieving the stored configuration

The ESP32's **non-volatile configuration is the shared source of truth**, while browser `localStorage` acts as a local editing copy. This allows a newly connected device to retrieve the current configuration instead of depending on the settings stored only in one browser.

## RTC Operation

The DS3231 is used specifically for **time-of-day scheduling**.

The distinction between the DS3231 and `millis()` is important:

- `millis()` measures elapsed time only while the ESP32 is running.
- The DS3231 provides the calendar/time reference for daily schedules and retains the clock when the ESP32 is unpowered.

The RTC was verified during development using an I2C scanner and a focused RTClib test. The DS3231 appeared at:

```text
0x68
```

The other observed I2C device at `0x57` was present during the scanner test but was not used as the RTC device.

## Fluid-System Design

The final prototype is **gravity-fed** and does not use a separate pump.

The water surface is elevated above the valve/outlet, creating hydrostatic head:

\[
P = \rho g h
\]

where:

- \(P\) = hydrostatic pressure contribution
- \(\rho\) = water density
- \(g\) = gravitational acceleration
- \(h\) = vertical water head

As an illustrative reference, a **9-inch vertical water head**:

```text
9 in = 0.2286 m
```

corresponds to approximately:

```text
2.24 kPa
≈ 0.33 psi
```

This is a reference calculation based on elevation difference, not a measured pressure value for every reservoir fill condition.

Actual flow depends on:

- reservoir water level
- valve elevation
- tubing resistance
- valve orifice
- fitting losses
- outlet geometry

The selected low/zero-differential-pressure solenoid arrangement is therefore appropriate for a gravity-fed concept where a conventional pressure-driven valve may not operate correctly at very low available pressure.

## Structural Design

The physical support structure uses blue PVC pipe with elbow and tee fittings.

The main square members are approximately:

```text
9 in × 9 in
```

with additional short and diagonal members used for support and bracing.

The structure supports:

- the reservoir
- the watering outlet
- the tubing
- the seedling tray area

The reservoir was based on an approximately **10-L container**. Water alone has an approximate mass of:

```text
10 kg
```

and an approximate weight of:

```text
W = mg
  ≈ 98.1 N
```

The nylon rope attachment points distribute the hanging load into the PVC support structure. Actual rope tension depends on attachment geometry and load distribution, so the prototype is considered a demonstration structure rather than a rated lifting frame.

The PVC joints were assembled without additional sealant because the fitted prototype was sufficiently rigid for the intended demonstration.

## Electronics Enclosure

The controller electronics were assembled inside a small clear acrylic enclosure.

The enclosure accommodates:

- controller board
- relay/driver circuitry
- wiring
- power input
- manual controls
- actuator connections

The enclosure was fabricated as a practical prototype rather than a production enclosure. Visible adhesive and cable management reflect the iterative construction process.

## Development and Iteration

The project was developed through repeated testing and redesign rather than a single build sequence.

### 1. Initial Mechanical Concept

The earliest concept involved transporting a seedling tray through a watering zone using a conveyor.

The concept was eventually abandoned because the added requirements for:

- motor control
- guides
- entry/exit sensing
- mechanical timing
- additional fabrication

made the system unnecessarily complex for the available prototype resources.

The final design retained the programmable watering concept but changed to a stationary seedling watering structure.

### 2. Wireless Communication

Bluetooth Low Energy was attempted first.

The prototype repeatedly experienced connection/disconnection behavior and browser GATT service retrieval failures. The communication design was therefore changed to **ESP32 SoftAP Wi-Fi + HTTP**, which preserved local wireless operation while simplifying browser interaction.

### 3. RTC Debugging

The main program initially reported the DS3231 as not detected even though the eventual hardware connection was correct.

An I2C scanner identified:

```text
0x57
0x68
```

A focused RTClib test then detected the DS3231 at `0x68` and returned a readable time.

### 4. Configuration Synchronization

The web interface initially did not consistently reconstruct schedules and environmental rules across different connected devices.

The final approach stores the configuration on the ESP32 using non-volatile storage. The browser maintains a local editing copy, while the ESP32 remains the authoritative stored configuration.

## Safety and Reliability

The prototype includes several protections and reliability measures:

- 30-minute maximum watering duration
- dedicated force-stop command
- relay coil flyback protection
- solenoid inductive-load flyback protection
- transistor drivers between control logic and actuator circuitry
- independent DS3231 time reference
- non-blocking elapsed-time logic
- non-volatile configuration storage
- low-pressure gravity-fed watering arrangement

The system is intended as a low-cost educational prototype, not a certified agricultural or load-bearing product.

## Estimated Project Cost

The documented/estimated project cost is approximately:

```text
₱1,235
```

This includes recorded purchase prices, approximate prices for unpriced components, an estimated **₱30** for three 2-pin terminal blocks, and a **₱300 miscellaneous/catch-up allowance** for solder, small parts, and other unpriced consumables.

The figure represents best-effort prototype spending rather than an exact manufacturing cost for a production unit.

## Project Status

The completed prototype demonstrates integrated operation of:

- embedded control
- environmental sensing
- real-time scheduling
- local wireless configuration
- relay/solenoid actuation
- gravity-fed water delivery
- PVC structural support
- acrylic electronics enclosure

The strongest result of the project is the integration of these subsystems into one functional prototype.

## Repository Contents

The repository contains the complete implementation and supporting project resources.

The full firmware and project files are maintained here so the complete source does not need to be reproduced in the printed project documentation.

## Author

**Rovic B. Magdamit**  
ECE - 3A  
University of Caloocan City  
College of Engineering  
Electronics Engineering Department

## Project Title

**ESP32-Based Smart Seedling Watering System with Automated Environmental Control**

## License

No software license has been specified yet. Unless a license is added to this repository, the project should be treated as the author's project work and reused according to the repository owner's permission.

## AI-Assisted Development

AI tools were used as development assistants for brainstorming, debugging, code review, technical writing, and documentation. Final implementation, hardware construction, testing, and project decisions were performed and verified by the author.