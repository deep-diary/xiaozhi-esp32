#pragma once

#include "handle/handle_app.h"

class HandleAppLog : public IHandleApp {
public:
    const char* Name() const override { return "log"; }
    void OnSnapshot(const HandleSnapshot& snap) override;
};
