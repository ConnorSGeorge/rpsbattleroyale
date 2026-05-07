#ifndef BOT_H
#define BOT_H

#include <cstdio>
#include "teenyat.h"

// Read memory addresses.
const unsigned short X = 0x9000;              // Current X coordinate.
const unsigned short Y = 0x9001;              // Current Y coordinate.
const unsigned short DETECT_PRED = 0x9010;    // Nearest bot that beats this bot.
const unsigned short DETECT_PREY = 0x9011;    // Nearest bot that this bot beats.
const unsigned short DETECT_SELF = 0x9012;    // Nearest bot of the same type.

// Write memory addresses.
const unsigned short MOVE_N = 0x9020;         // Move north (decrease Y).
const unsigned short MOVE_E = 0x9021;         // Move east (increase X).
const unsigned short MOVE_S = 0x9022;         // Move south (increase Y).
const unsigned short MOVE_W = 0x9023;         // Move west (decrease X).

/**
 * Represents one bot in the simulation.
 */
struct bot {
    int x;
    int y;
    int type;
    int defeats;         // Type this bot defeats.
    int defeatedBy;      // Type that defeats this bot.
    teenyat* t;          // TeenyAT VM instance.
};

#endif // BOT_H
