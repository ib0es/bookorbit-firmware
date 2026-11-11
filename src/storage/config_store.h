#pragma once

struct AppConfig;

// The settings file /config.json, with the network and account fields
// mirrored to NVS so they survive a card format or swap.
namespace ConfigStore {
// Only fields present in the file are overwritten, so the defaults of
// AppConfig survive a partial config. With no config on the card the
// network settings come from NVS (and a new config.json is written).
bool load(AppConfig& cfg);
bool save(const AppConfig& cfg);
}
