/* Wireless Smoke & Heat Detector
 * Ningbo Siterwell Electronics  GS 558  Sw. V05  Ver. 1.3  on 433.885MHz
 * VisorTech RWM-460.f  Sw. V05, distributed by PEARL, seen on 433.674MHz
 *
 * A short wakeup pulse followed by a wide gap (11764 us gap),
 * followed by 24 data pulses and 2 short stop pulses (in a single bit width).
 * This is repeated 8 times with the next wakeup directly following
 * the preceding stop pulses.
 *
 * Bit width is 1731 us with
 * Short pulse: -___ 436 us pulse + 1299 us gap
 * Long pulse:  ---_ 1202 us pulse + 526 us gap
 * Stop pulse:  -_-_ 434us pulse + 434us gap + 434us pulse + 434us gap
 * = 2300 baud pulse width / 578 baud bit width
 *
 * 24 bits (6 nibbles):
 * - first 5 bits are unit number with bits reversed
 * - next 15(?) bits are group id, likely also reversed
 * - last 4 bits are always 0x3 (maybe hardware/protocol version)
 * Decoding will reverse the whole packet.
 * Short pulses are 0, long pulses 1, need to invert the demod output.
 *
 * Each device has it's own group id and unit number as well as a
 * shared/learned group id and unit number.
 * In learn mode the primary will offer it's group id and the next unit number.
 * The secondary device acknowledges pairing with 16 0x555555 packets
 * and copies the offered shared group id and unit number.
 * The primary device then increases it's unit number.
 * This means the primary will always have the same unit number as the
 * last learned secondary, weird.
 * Also you always need to learn from the same primary.
 *
 * Copyright (C) 2017 Christian W. Zuckschwerdt <zany@triq.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "decoder.h"

static unsigned char reverse_byte(unsigned char b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

static int smoke_gs558_callback(bitbuffer_t *bitbuffer) {
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];
    uint8_t *b;
    int r;
    int learn = 0;
    int unit; // max 30
    int id;
    char code_str[7];

    if (bitbuffer->num_rows < 3)
        return 0; // truncated transmission

    bitbuffer_invert(bitbuffer);

    for (r = 0; r < bitbuffer->num_rows; ++r) {
        b = bitbuffer->bb[r];

        // count learn pattern and strip
        if (bitbuffer->bits_per_row[r] >= 24
                && b[0] == 0x55 && b[1] == 0x55 && b[2] == 0x55) {
            ++learn;
            bitbuffer->bits_per_row[r] = 0;
        }

        // strip end-of-packet pulse
        if ((bitbuffer->bits_per_row[r] == 26 || bitbuffer->bits_per_row[r] == 27)
                && b[3] == 0)
            bitbuffer->bits_per_row[r] = 24;
    }

    r = bitbuffer_find_repeated_row(bitbuffer, 3, 24);

    if (r < 0)
        return 0;

    b = bitbuffer->bb[r];

    // if ((b[2] & 0x0f) != 0x03)
    //     return 0; // last nibble is always 0x3?

    b[0] = reverse_byte(b[0]);
    b[1] = reverse_byte(b[1]);
    b[2] = reverse_byte(b[2]);

    unit = b[0] & 0x1f; // 5 bits
    id = ((b[2] & 0x0f) << 11) | (b[1] << 3) | (b[0] >> 5); // 15 bits

    if (id == 0 || id == 0x7fff)
         return 0; // reject min/max to reduce false positives

    sprintf(code_str, "%02x%02x%02x", b[2], b[1], b[0]);

    /* Get time now */
    local_time_str(0, time_str);
    data = data_make(
        "time",          "",            DATA_STRING, time_str,
        "model",         "",            DATA_STRING, "Smoke detector GS 558",
        "id"   ,         "",            DATA_INT, id,
        "unit",          "",            DATA_INT, unit,
        "learn",         "",            DATA_INT, learn > 1,
        "code",          "Raw Code",    DATA_STRING, code_str,
        NULL);
    data_acquired_handler(data);

    return 1;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "unit",
    "learn",
    "code",
    NULL
};

r_device smoke_gs558 = {
    .name           = "Wireless Smoke and Heat Detector GS 558",
    .modulation     = OOK_PULSE_PWM_RAW,
    .short_limit    = (436+1202)/2, // Threshold between short and long pulse [us]
    .long_limit     = 1299*1.5, // Maximum gap size before new row of bits [us]
    .reset_limit    = 11764*1.5, // Maximum gap size before End Of Message [us]
    .json_callback  = &smoke_gs558_callback,
    .disabled       = 0,
    .fields         = output_fields
};
