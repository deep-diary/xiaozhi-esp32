#pragma once

#include "handle/handle_types.h"

class IHandleApp {
public:
    virtual ~IHandleApp() = default;
    virtual const char* Name() const = 0;
    virtual void OnSnapshot(const HandleSnapshot& snap) = 0;
};
