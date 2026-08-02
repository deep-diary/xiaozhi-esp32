#pragma once

#include "handle/handle_app.h"
#include "handle/handle_config.h"
#include "handle/handle_event_hub.h"

#if DEEP_DOG_DOG_ENABLE
class DogControl;
#endif

class HandleAppDog : public IHandleApp {
public:
#if DEEP_DOG_DOG_ENABLE
    HandleAppDog(DogControl* dog, HandleEventHub* hub);
#else
    explicit HandleAppDog(HandleEventHub* hub);
#endif

    const char* Name() const override { return "dog"; }
    void OnSnapshot(const HandleSnapshot& snap) override;

private:
    HandleEventHub* hub_ = nullptr;
#if DEEP_DOG_DOG_ENABLE
    DogControl* dog_ = nullptr;
    bool prev_start_ = false;
    bool prev_b_ = false;
    int move_dir_ = 0;  // -1 back, 0 stop, +1 forward
#endif
};
