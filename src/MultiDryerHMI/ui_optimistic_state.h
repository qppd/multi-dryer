#ifndef UI_OPTIMISTIC_STATE_H
#define UI_OPTIMISTIC_STATE_H

#include "dryer_data.h"

struct UiOptimisticState {
    bool active;
    unsigned long untilMs;
    DryerState systemState;
    bool heaterOn;
    bool fanOn;
    bool exhaustOn;
    bool showStart;
    bool showStop;
    bool showPause;
    bool showResume;
};

extern UiOptimisticState gUiOptimisticState;

void uiOptimisticSet(DryerState state, bool heaterOn, bool fanOn, bool exhaustOn,
                     bool showStart, bool showStop, bool showPause, bool showResume,
                     unsigned long graceMs = 15000UL);
bool uiOptimisticIsActive();
void uiOptimisticClear();

#endif // UI_OPTIMISTIC_STATE_H
