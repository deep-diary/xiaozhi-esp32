#ifndef GIMBAL_CONTROL_H
#define GIMBAL_CONTROL_H

#include "mcp_server.h"
#include "Gimbal.h"

class GimbalControl {
private:
    Gimbal_t* gimbal_;

public:
    explicit GimbalControl(Gimbal_t* gimbal, McpServer& mcp_server);
}; 

#endif // GIMBAL_CONTROL_H
