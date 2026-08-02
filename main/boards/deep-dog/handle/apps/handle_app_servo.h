#pragma once

#include "handle/handle_app.h"

class HandleAppServo : public IHandleApp {
public:
    const char* Name() const override { return "servo"; }
    void OnSnapshot(const HandleSnapshot& snap) override;
};
