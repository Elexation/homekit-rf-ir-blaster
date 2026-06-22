#pragma once

namespace config { struct Config; }

void buildAccessories(const config::Config& cfg);   // call once, after homeSpan.begin()
void applyConfigChange(const config::Config& cfg);  // live add/remove/rename; no caller yet
void pollPendingSends();                            // call from loop(); drives scheduled repeats

// Command names a device's slots must use, by service type:
//   power (Switch/Outlet/LightBulb/Fan/Television): on, off, toggle
//   WindowCovering: up, down, stop
//   TV volume: volume_up, volume_down
//   TV keys: key_up key_down key_left key_right key_select key_back key_play_pause key_info
