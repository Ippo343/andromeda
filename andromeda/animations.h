#ifndef ANIMATIONS_H
#define ANIMATIONS_H

#include "animation-base.h"

class WiFiConnectingAnimation : public AbstractAnimation
{
  public:
    virtual const char* GetName();
    
    void run() override;
};

class WiFiSuccessAnimation : public AbstractAnimation
{
  public:
    virtual const char* GetName();
    
    void run() override;
};

class ErrorAnimation : public AbstractAnimation
{
  public:
    virtual const char* GetName();
    
    void run() override;
};

// Factory function to get a random animation instance
AbstractAnimation* getRandomAnimation();

#endif