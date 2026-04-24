# Power Monitor
Monitors grid power presence
## Hardware

- ESP32 ([Ideaboard by CRCibernetica](https://github.com/CRCibernetica/circuitpython-ideaboard/wiki/1.-Overview))
- Buzzer
- USB 5V charger
- Voltage divider (5V → 3.3V)
- 4 X AA Battery Holder
- 4 AA Batteries
- Stripped USB Cable
---
## How It Works

- USB charger outputs 5V when grid power is present, 
- 5V goes to a voltage divider to 3.3v
- ESP32 reads that signal:
  - HIGH → power present
  - LOW → power lost

---

## Behavior

### Power Loss

- Triggers buzzer + LED alarm
- Connects to WiFi
- Sends POST request
- Waits for power restore or sleeps

### Power Restored

- Waits for stable power
- Connects to WiFi
- Sends Wake-on-LAN packets
- Checks if host responds
- Sleeps again

---

## Configuration

Edit in code:

```cpp
#define SSID            "YOUR_WIFI_SSID"
#define PASS            "YOUR_WIFI_PASSWORD"
#define POST_URL        "https://example.com/api/endpoint"
#define PING_IP         "192.168.1.100"

#define WOL_BROADCAST   "192.168.1.255"
uint8_t WOL_MAC[] = {0x00,0x00,0x00,0x00,0x00,0x00};
