#ifndef _STICK_MODE_SWITCHER_H_
#define _STICK_MODE_SWITCHER_H_

#include "gpaddon.h"
#include "GamepadEnums.h"
#include "BoardConfig.h"
#include "enums.pb.h"
#include "config.pb.h"
#include "types.h"

/*
 * StickModeSwitcher Add-on
 *
 * A single potentiometer (analog) stick that can be switched between three
 * output functions with one GPIO button:
 *
 *   1. Left analog stick  (gamepad->state.lx / ly)   [XInput gamepad]
 *   2. Right analog stick (gamepad->state.rx / ry)   [XInput gamepad]
 *   3. Keyboard WASD      (stick deflection drives dpad, which the built-in
 *                         KeyboardDriver turns into W/A/S/D key reports)
 *
 * Every press of the cycle button moves to the next mode. The required USB
 * input mode follows the mode automatically: the two analog modes run on the
 * XInput gamepad driver, WASD runs on the Keyboard driver. Switching between
 * the two analog modes is instant; crossing the XInput<->Keyboard boundary
 * saves gamepadOptions.inputMode and reboots (same save+reboot sequence the
 * web configurator uses for input mode changes).
 *
 * The selected mode is persisted to flash on every button press
 * (savedMode in StickModeSwitcherOptions), so the add-on resumes the exact
 * mode -- and matching USB input mode -- after a power cycle.
 *
 * All potentiometer/calibration settings (pins, auto/manual calibration,
 * inner/outer deadzone, forced circularity, smoothing, error rate, invert)
 * are REUSED from the native Analog add-on's AnalogOptions, so there is no
 * duplicated stick configuration UI. The native Analog add-on itself should
 * be left disabled in the web configurator while this add-on is enabled.
 *
 * WASD mode is edge-like: pushing the stick out of the deadzone presses the
 * key once (held until released) and returning to center releases it. When
 * switching away from WASD mode any currently held key is explicitly
 * released so no key ever gets stuck.
 */

#ifndef STICK_MODE_SWITCHER_ENABLED
#define STICK_MODE_SWITCHER_ENABLED 0
#endif

// GPIO for the mode cycle button (pull-up, active low). Default 18.
#ifndef STICK_MODE_SWITCHER_BUTTON_PIN
#define STICK_MODE_SWITCHER_BUTTON_PIN 18
#endif

// Which native Analog stick pair provides the physical potentiometer input:
//   0 = Analog Stick 1 (analogAdc1* options)  [default]
//   1 = Analog Stick 2 (analogAdc2* options)
#ifndef STICK_MODE_SWITCHER_ADC_STICK
#define STICK_MODE_SWITCHER_ADC_STICK 0
#endif

// Output modes cycled by the mode button
#define STICK_MODE_SWITCHER_MODE_LEFT_ANALOG  0
#define STICK_MODE_SWITCHER_MODE_RIGHT_ANALOG 1
#define STICK_MODE_SWITCHER_MODE_WASD         2
#define STICK_MODE_SWITCHER_MODE_COUNT        3

// Mode applied on a fresh install before the user has ever pressed the button
#define STICK_MODE_SWITCHER_DEFAULT_MODE STICK_MODE_SWITCHER_MODE_LEFT_ANALOG

// Minimum deflection (0..1, relative to center 0.5) required to register a
// WASD direction. Guards against spurious diagonals from radial-deadzone
// rescaling right at the deadzone edge.
#ifndef STICK_MODE_SWITCHER_WASD_THRESHOLD
#define STICK_MODE_SWITCHER_WASD_THRESHOLD 0.10f
#endif

// Mode button debounce window (milliseconds)
#ifndef STICK_MODE_SWITCHER_BUTTON_DEBOUNCE_MS
#define STICK_MODE_SWITCHER_BUTTON_DEBOUNCE_MS 5
#endif

// StickModeSwitcher Module Name
#define StickModeSwitcherName "StickModeSwitcher"

class StickModeSwitcherAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void preprocess() {}
    virtual void process();
    virtual void postprocess(bool sent) {}
    virtual void reinit() {}
    virtual std::string name() { return StickModeSwitcherName; }
private:
    struct StickData {
        Pin_t x_pin;
        Pin_t y_pin;
        Pin_t x_pin_adc;
        Pin_t y_pin_adc;
        float x_value;
        float y_value;
        uint32_t x_center;
        uint32_t y_center;
        uint32_t x_min;
        uint32_t x_max;
        uint32_t y_min;
        uint32_t y_max;
        float xy_magnitude;
        float x_magnitude;
        float y_magnitude;
        InvertMode analog_invert;
        float x_ema;
        float y_ema;
        bool x_ema_initialized;
        bool y_ema_initialized;
        bool ema_option;
        float ema_smoothing;
        float error_rate;
        float in_deadzone;
        float out_deadzone;
        bool auto_calibration;
        bool forced_circularity;
        uint32_t joystick_center_x;
        uint32_t joystick_center_y;
    };

    void handleModeButton(Gamepad *gamepad);
    void applyWasdMapping(bool enable);
    void writeAnalogOutput(Gamepad *gamepad, uint32_t joystickMax, bool leftStick);
    void writeWasdOutput(Gamepad *gamepad);

    float normalizePin(uint16_t adc_value, uint32_t center, uint32_t minimum, uint32_t maximum);
    float emaCalculation(float ema_value, float ema_previous);
    float magnitudeCalculation();
    void radialDeadzone();

    StickData stick;
    float inverse_deadzone_span;
    uint32_t modeButtonPin;
    uint8_t currentMode;
    uint8_t lastRawButton;
    uint32_t buttonChangeTime;
    bool buttonWasPressed;

    // WASD keyboard mapping override tracking
    bool wasdMappingApplied;
    KeyboardMapping *wasdMappingTarget;
    uint32_t originalKeyDpadUp;
    uint32_t originalKeyDpadDown;
    uint32_t originalKeyDpadLeft;
    uint32_t originalKeyDpadRight;
};

#endif  // _STICK_MODE_SWITCHER_H_
