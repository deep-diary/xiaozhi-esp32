#pragma once

#include "handle/handle_app.h"
#include "handle/handle_config.h"
#include "handle/handle_event_hub.h"
#include "handle/keymap_store.h"

class HandleAppKeyMap : public IHandleApp {
public:
    explicit HandleAppKeyMap(HandleEventHub* hub);

    const char* Name() const override { return "keymap"; }
    void OnSnapshot(const HandleSnapshot& snap) override;

private:
    void Fire(const HandleActionBinding_t& act, bool hold);
    void OnKeyEdge(HandleKeyIndex_t key, bool now, bool prev);

    HandleEventHub* hub_ = nullptr;
    bool prev_[HANDLE_KEY_COUNT] {};
    bool hold_active_[HANDLE_KEY_COUNT] {};
};
