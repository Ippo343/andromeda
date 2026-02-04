#include "mission-control.h"

// Initialize the web command queue
void MissionControl::initWebQueue()
{
  webCommandQueue = xQueueCreate(WEB_QUEUE_SIZE, sizeof(Command));
  if (webCommandQueue == nullptr) {
    Log.errorln("Failed to create web command queue");
  }
}

// Process any pending web commands
void MissionControl::processWebCommands()
{
  if (webCommandQueue == nullptr) {
    Log.errorln("Web command queue is not initialized");
    return;
  }

  Command command;
  while (xQueueReceive(webCommandQueue, &command, 0) == pdTRUE)
  {
    Log.noticeln("Processing web command: %c", command);

    switch (command)
    {
      case Command::NEXT:
        handleTransition();
        break;
      case Command::HOLD:
        holdEffect();
        break;
      case Command::POWER_OFF:
        powerOff();
        break;
      case Command::WHITE:
        staticWhite();
        break;
      default:
        Log.warningln("Unknown web command: %c", static_cast<char>(command));
        break;
    }
  }
}

// Queue a web command from the web server
bool MissionControl::queueWebCommand(Command command)
{
  if (webCommandQueue == nullptr) {
    Log.errorln("Web command queue is not initialized");
    return false;
  }

  if (xQueueSend(webCommandQueue, &command, 0) == pdTRUE) {
    return true;
  } else {
    Log.warningln("Web command queue full, dropping command: %c", command);
    return false;
  }
}

// Picks a new transition time and resets the other timestamps accordingly
void MissionControl::setNextTransition()
{
  effectStart = millis();
  fadeInEnd = effectStart + FADE_IN_DURATION;
  fadeOutStart = fadeInEnd + random(MIN_EFFECT_DURATION, MAX_EFFECT_DURATION);
  nextTransition = fadeOutStart + FADE_OUT_DURATION;

  Log.noticeln("Next transition in %d ms", nextTransition);
}

// Return the master brightness to fade the effects in and out
//
//   fadeInEnd   fadeOutStart
//     /¯¯¯¯¯¯¯¯¯¯¯¯¯¯\
//    /                \
// effectStart      nextTransition
//
byte MissionControl::calcBrightness(milliseconds_t t)
{
  byte brightness = 0;

  // Most of the time is spent in the middle so test that first
  // we are all about microseconds in this highly efficient architecture
  if (t >= fadeInEnd && t <= fadeOutStart)
  {
    brightness = this->maxBrightness;
  }
  else if (t < fadeInEnd)
  {
    milliseconds_t dt = (t - effectStart);
    brightness = map(dt, 0, FADE_IN_DURATION, 0, this->maxBrightness);
  }
  else if (t > fadeOutStart)
  {
    milliseconds_t dt = (t - fadeOutStart);
    brightness = map(dt, 0, FADE_OUT_DURATION, this->maxBrightness, 0);
  }

  return dim8_raw(constrain(brightness, 0, 255));
}

// Pick a new random animation, play it, and deallocate it
void MissionControl::runRandomAnimation()
{
  AbstractAnimation* animation = getRandomAnimation();

  Log.noticeln("Picked new animation: %s", animation->GetName());

  if (animation->controlHints & ControlHints::ROTATE_SPACE)
    GEOMETRY.applyGlobalRandomRotation();
  else
    GEOMETRY.resetGlobalTransform();

  // First fade everything out to black and add a small delay
  // to create some separation from the effect
  FastLED.setBrightness(0);
  delay(200);

  // Reset the brightness to max and give control back to the animation
  FastLED.setBrightness(MissionControl::Instance().getMaxBrightness());
  animation->run();
  animation->cleanup();

  // Turn it down to zero and wait a little bit.
  // This creates another small separation before the effect.
  // The brightness must start from zero to avoid ugly jumps when the lerp kicks in
  FastLED.setBrightness(0);
  delay(200);

  delete animation;
  animation = NULL;
}

// When the transition time is reached:
// - play an animation
// - pick a new effect (unless one is passed in)
// - pick the next transition time
// - and also sprinkle random rotation transforms here and there
void MissionControl::handleTransition(AbstractEffect* nextEffect, bool playAnimation)
{
  Log.noticeln("Handling transition");

  ON = true;  // Ensure the system is ON

  if (playAnimation)
    runRandomAnimation();

  // Delete the current effect if it exists
  if (effect)
    delete effect;

  // If a next effect is provided, use it, otherwise pick a new random effect
  if (nextEffect)
    effect = nextEffect;
  else
    effect = getRandomEffect();

  Log.noticeln("Picked new effect: %s", effect->GetName());

  if (effect->controlHints & ControlHints::ROTATE_SPACE)
    GEOMETRY.applyGlobalRandomRotation();
  else
    GEOMETRY.resetGlobalTransform();

  setNextTransition();
}

void MissionControl::holdEffect()
{
  // Set nextTransition to the maximum possible value,
  // so that it's never reached and the current effect is held forever.
  // You also need to set the timing of the fade out ramp to hold the brightness at max.
  // Note that using Next from the web UI resets the transition and restarts the cycle.
  nextTransition = ~0UL;
  fadeOutStart   = ~0UL;

  Log.noticeln("Holding current effect forever");
}

void MissionControl::powerOff()
{
  // Immediately switch off all the lights and prevent further updates
  paint(CRGB::Black);
  FASTLED_SHOW();
  ON = false;
  PerformanceMonitor::Instance().stop();
}

void MissionControl::update(milliseconds_t t)
{
  processWebCommands();

  if (!ON)
    return;

  Energy::set(slowSin(millis(), 0.5, 0, 255));

  if (t >= nextTransition)
  {
    handleTransition();
    return;
  }

  FastLED.setBrightness(calcBrightness(t));

  effect->precompute(t);

  // TODO: Update based on your effect signature
  // Option A: If effects expect LedStrip array pointer:
  // effect->render(STRIPS, t);
  // You'll need to update effects to use GEOMETRY.getStrip() internally
  //
  // Option B: If you update effect signatures to not need strips parameter:
  // effect->render(t);
  //
  // For now, keeping original pattern - see EFFECTS_MIGRATION.md for details
  effect->render(t);

  effect->postprocess(t);

  FASTLED_SHOW();
}

void MissionControl::staticWhite()
{
  handleTransition(new StaticWhite(), false);
  holdEffect();
}
