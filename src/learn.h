#pragma once

#include "config_model.h"
#include "learn_machine.h"

void startLearn();  // arms every band (IR + RF); the machine auto-detects the source
void cancelLearn();
void pollLearn();  // drains captured bursts into the machine + ticks the window; call from loop()

learn::State learnState();
learn::FailReason learnFailReason();    // valid when learnState() == Failed
config::StoredCode takeLearnedCode();  // Captured -> Idle handover; else unlearned
