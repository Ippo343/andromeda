#pragma once

#include "animation-base.h"
#include "animation-frame-base.h"
#include "effects-utils.h"
#include "geometry/geometry.h"
#include "perf-monitor.h"

class WiFiConnectingAnimation : public AbstractBlockingAnimation
{
   public:
    virtual const char* GetName();

    void run() override;
};

class WiFiSuccessAnimation : public AbstractBlockingAnimation
{
   public:
    virtual const char* GetName();

    void run() override;
};

class ErrorAnimation : public AbstractBlockingAnimation
{
   public:
    virtual const char* GetName();

    void run() override;
};

// Factory function to get a random rotation animation instance
AbstractFrameAnimation* getRandomAnimation();
