#ifndef GLITCH_SHAPE_H
#define GLITCH_SHAPE_H

/*  WIDE GENTOO SWIRL (4 lines tall, fixed-width rows) */

/* Base shape (all rows are 39 chars wide) */

#define SHAPE1    "        ___------_______               "
#define SHAPE2    "    ____/   .-.    .-.   \\____         "
#define SHAPE3    "  _/  _  \\  (   )  (   )  /  _  \\_     "
#define SHAPE4    "  \\______/\\__\\_/____\\_/__/\\______/     "

/*
 * For stable colors:
 *  - We only glitch rows 1, 3, 4 (SHAPE1/3/4_S*)
 *  - Row 2 (SHAPE2_S*) is forced to match SHAPE2 exactly,
 *    so the 'ker |' column never moves horizontally.
 */

/* Glitch phases for row 1 */

#define SHAPE1_S1 "        _--~~~---______               "
#define SHAPE1_S2 "        ___------______               "
#define SHAPE1_S3 "        ___------_______              "

/* Row 2: keep ALL glitch variants identical to SHAPE2 for alignment */

#define SHAPE2_S1 SHAPE2
#define SHAPE2_S2 SHAPE2
#define SHAPE2_S3 SHAPE2

/* Glitch phases for row 3 */

#define SHAPE3_S1 "  _/  _  \\  (  _)(   )  /  _  \\_     "
#define SHAPE3_S2 "  _/  _  \\  (_  ) ( _ )  /  _  \\_    "
#define SHAPE3_S3 "  _/  _  \\  (   )  (   )  /  _  \\_   "

/* Glitch phases for row 4 */

#define SHAPE4_S1 "  \\______/\\__\\_/ _--\\_/__/\\______/    "
#define SHAPE4_S2 "  \\______/\\__\\_/-___\\_/__/\\______/    "
#define SHAPE4_S3 "  \\______/\\__\\_/____\\_/__/\\______/    "

#endif
