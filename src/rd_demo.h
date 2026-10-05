/* A scripted walk through one fixed world, used to record the same moment
 * of play on every platform. Each platform calls rd_demo_start() once and
 * then rd_demo_step() to advance. Given the same seed and inputs the core
 * is deterministic, so frame n shows the same thing everywhere. */
#ifndef RD_DEMO_H
#define RD_DEMO_H
#include "rd.h"

#define RD_DEMO_TICKS 1500 /* 25 seconds at 60 Hz */

int rd_demo_start(void);        /* builds the world and returns the player id */
int rd_demo_step(int ticks);    /* advances, returns the tick reached */

#endif
