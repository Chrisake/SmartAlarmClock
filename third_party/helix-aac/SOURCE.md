# Helix AAC decoder

Fixed-point AAC-LC and HE-AAC (SBR) decoder by RealNetworks, used by the radio
player for AAC streams. Licensed under the RealNetworks Public Source License
(RPSL.txt), or the Community Source License (RCSL.txt) -- see LICENSE.txt.

Taken from ESP8266Audio's copy, `src/libhelix-aac`, at commit
74fc1f09bbba5e5c5450b445452ba64ef2d8bbad
(https://github.com/earlephilhower/ESP8266Audio), with these changes, each
marked `SmartAlarmClock` in the source:

- `bitstream.c`: `RefillBitstreamCache()` reads nothing when the bytes left
  have gone negative. Helix does not check a frame against its input, so a
  damaged or truncated frame drove that count below zero and the refill loop
  then read far past the buffer. The player also hands the decoder only whole
  ADTS frames.

- `aaccommon.h`: `Arduino.h` and `pgmspace.h` are included only under Arduino,
  and `PROGMEM` is defined empty otherwise.
- `assembly.h`: `PROGMEM` is defined empty outside Arduino; the 32-bit MSVC
  inline-assembly branch is skipped on 64-bit Windows, and 64-bit Windows and
  RISC-V (the ESP32-P4) use the portable C branch.

Build with `USE_DEFAULT_STDLIB` defined, so `buffers.c` and `sbr.c` use the C
library rather than Helix's own `hlxclib`.
