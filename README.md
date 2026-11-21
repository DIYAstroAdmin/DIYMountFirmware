# MyMount

Firmware for a German equatorial telescope mount driven by two NEMA stepper motors,
running on a FYSETC E4 board (ESP32 with four TMC2209 drivers).

- LX200 protocol on the USB serial port
- ASCOM Alpaca (Telescope, interface version 4) over WiFi, with discovery
- Web page for control and configuration
- Goto, sync, tracking (sidereal, lunar, solar), pulse guiding, manual moves
- Homing with switches on the X-MIN and Y-MIN connectors
- No automatic meridian flip, no hand controller, no ST4 port

## Wiring

| Function        | E4 connector | GPIO |
|-----------------|--------------|------|
| RA motor        | X            | step 27, dir 26 |
| Dec motor       | Y            | step 33, dir 32 |
| RA home switch  | X-MIN        | 34 |
| Dec home switch | Y-MIN        | 35 |

The Z and E0 drivers are not used.

## Configuration

Hardware settings are in [include/Config.h](include/Config.h). Those marked `[web]` there are
only defaults: they can be changed on the web page, are kept in flash, and then take
precedence over the file.

Set on the web page (**Axes** panel), per axis:

| Setting | When it takes effect |
|---------|----------------------|
| Microsteps, gear ratio, reverse direction | At the next restart |
| Motor current | Immediately |
| Home search direction, home switch angle | At the next *Find home* |

The gear ratio is the total reduction: motor revolutions per revolution of the axis.
Restarting resets the mount position to home, so park the mount before restarting.
If the microsteps and gear ratio would need more than 200,000 steps/s at the slew rate,
that axis is slowed down; the panel shows each axis' maximum speed.

Set in `Config.h` only: motor step angle, slew speed limit and acceleration, home switch
polarity and maximum search travel, pins and driver addresses.

Site, WiFi and rate defaults are also in `Config.h` and can be changed on the
**Configuration** panel.

Build and upload with PlatformIO:

```
pio run -t upload
```

## Start-up position

The controller has no encoders. At power-up it assumes the mount is in its **home position:
counterweight down, telescope pointing at the celestial pole**. Either put the mount there
before switching on, use *Find home* (switches), or move it there and press *Set home here*.

## Checking motor directions

Do this once, from the web page, with the mount at home:

1. Turn tracking on. The RA axis must turn the way the sky moves (towards the west).
   If it turns the other way, tick *Reverse direction* for RA on the Axes panel, save and restart.
2. Go back to home and *Set home here*. Hold **S** at the find rate. The telescope must tip
   over towards the **western** horizon. If it tips east, reverse Dec the same way.

## Connecting

**WiFi.** With no network configured the board starts an access point named `mymount`
(password `mymount123`); the web page is at `http://192.168.4.1/`. Enter your network on the
configuration panel, save and restart. After that the page is at `http://mymount.local/`.

**Time and place.** Pointing depends on the clock and the site coordinates. The time is
taken from NTP when the board is on a network with internet access, from the browser when
the web page is opened, or from the LX200/Alpaca client. Set latitude and longitude on the
web page.

**Alpaca.** Port 80, device `telescope/0`. Clients find it through Alpaca discovery.
Slews are asynchronous only (`SlewToCoordinatesAsync`, `SlewToTargetAsync`).

**LX200.** USB serial, 9600 baud, 8N1. Supported commands:

| Group     | Commands |
|-----------|----------|
| Position  | `:GR# :GD# :GA# :GZ# :GS# :Gm#` |
| Target    | `:Sr# :Sd# :Gr# :Gd# :MS# :CM# :CS#` |
| Motion    | `:Mn# :Ms# :Me# :Mw# :Qn# :Qs# :Qe# :Qw# :Q# :RG# :RC# :RM# :RS# :D#` |
| Guiding   | `:Mgn# :Mgs# :Mge# :Mgw#` (duration in ms) |
| Tracking  | `:Te# :Td# :TQ# :TL# :TS#` |
| Home/park | `:hF#` find home switches, `:hP#` park, `:hW#` unpark |
| Site/time | `:St# :Gt# :Sg# :Gg# :SG# :GG# :SL# :GL# :SC# :GC#`, elevation `:Sv±DDDD.D# :Gv#` (metres, OnStep extension) |
| Info      | `:GVP# :GVN# :GVD# :GVT# :GW#`, ACK (0x06) |

## Behaviour to know about

- **Meridian.** A goto picks the pier side that keeps the counterweight down. Tracking carries
  on past the meridian by the configured limit (default 5 degrees) and then stops. To continue,
  issue the goto again and the mount will swap sides.
- **Park** slews to the home position and stops tracking. Manual moves and gotos are refused
  until the mount is unparked.
- **Homing** runs past each switch at the fast rate before backing off and approaching slowly,
  so use switches that the axis can travel over (optical or hall sensors, or a cam).
- **Serial resets.** Opening the USB serial port can reset the ESP32 through the DTR line,
  which loses the mount position. Disable DTR/RTS in the client if it has the option.

## Source layout

| File | Purpose |
|------|---------|
| `include/Config.h` | Hardware and default settings |
| `src/Axis.*` | TMC2209 setup and step generation for one axis |
| `src/Mount.*` | Pointing model, goto, tracking, guiding, homing |
| `src/Astro.*` | Clock, sidereal time, coordinate helpers |
| `src/LX200.*` | LX200 serial protocol |
| `src/Alpaca.*` | ASCOM Alpaca API and discovery |
| `src/WebUI.*`, `src/WebPage.h` | Web server, JSON API and the page |
| `src/Settings.*` | Settings stored in flash |
| `src/Log.*` | Log buffer shown on the web page (`LOGD`/`LOGI`/`LOGW`/`LOGE`) |
