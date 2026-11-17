#ifndef BOARD_DISPLAYS_H
#define BOARD_DISPLAYS_H

#include "config/config.h"

class BoardDisplays {
public:
    BoardDisplays();

#if defined(BOARD_NUCLEARCOUNTER)
    void initNuclearCounter(const char* ssid);
#endif
};

#endif
