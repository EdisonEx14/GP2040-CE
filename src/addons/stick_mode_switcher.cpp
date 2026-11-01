#include "addons/stick_mode_switcher.h"
#include "config.pb.h"
#include "enums.pb.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "helper.h"
#include "storagemanager.h"
#include "drivermanager.h"
#include "eventmanager.h"
#include "events/GPRestartEvent.h"
#include "system.h"
#include "pico/platform.h"

#include <algorithm>
#include <math.h>

#define ADC_MAX ((1 << 12) - 1) // 4095
#define ANALOG_MAX 1.0f
#define ANALOG_CENTER 0.5f
#define ANALOG_MINIMUM 0.0f
#define ANALOG_MIN_DEADZONE_BAND 0.01f

// RP2040 ADC channels start at GPIO 26 (ADC0). Mirrors the native Analog
// add-on's valid-range check (GPIO 26..28).
#define STICK_MODE_SWITCHER_ADC_GPIO_OFFSET 26

static inline bool isAdcPin(Pin_t pin) {
    return pin >= static_cast<Pin_t>(STICK_MODE_SWITCHER_ADC_GPIO_OFFSET) &&
           pin < static_cast<Pin_t>(STICK_MODE_SWITCHER_ADC_GPIO_OFFSET + NUM_ADC_CHANNELS - 1);
}

bool StickModeSwitcherAddon::available() {
    const StickModeSwitcherOptions& options = Storage::getInstance().getAddonOptions().stickModeSwitcherOptions;
    if (!options.enabled) {
        return false;
    }

    // We need a configured potentiometer stick from the native Analog options.
    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;
    Pin_t xPin = (STICK_MODE_SWITCHER_ADC_STICK == 0) ? analogOptions.analogAdc1PinX : analogOptions.analogAdc2PinX;
    Pin_t yPin = (STICK_MODE_SWITCHER_ADC_STICK == 0) ? analogOptions.analogAdc1PinY : analogOptions.analogAdc2PinY;
    return (isValidPin(xPin) && isAdcPin(xPin)) || (isValidPin(yPin) && isAdcPin(yPin));
}

void StickModeSwitcherAddon::setup() {
    const StickModeSwitcherOptions& options = Storage::getInstance().getAddonOptions().stickModeSwitcherOptions;
    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;

    // ---- Mode cycle button (pull-up, active low) ----
    modeButtonPin = isValidPin(options.modeButtonPin) ? options.modeButtonPin : (Pin_t)-1;
    if (isValidPin(modeButtonPin)) {
        gpio_init(modeButtonPin);
        gpio_set_dir(modeButtonPin, GPIO_IN);
        gpio_pull_up(modeButtonPin);
    }
    lastRawButton = 1;          // pull-up reads high when released
    buttonChangeTime = getMillis();
    buttonWasPressed = false;

    // ---- Load every potentiometer setting from the native Analog add-on ----
    // (pins, calibration, deadzones, forced circularity, smoothing, error
    // rate, invert). Which stick pair is selected by STICK_MODE_SWITCHER_ADC_STICK.
    const bool useStick2 = (STICK_MODE_SWITCHER_ADC_STICK != 0);
    stick.x_pin = useStick2 ? analogOptions.analogAdc2PinX : analogOptions.analogAdc1PinX;
    stick.y_pin = useStick2 ? analogOptions.analogAdc2PinY : analogOptions.analogAdc1PinY;
    stick.analog_invert = useStick2 ? analogOptions.analogAdc2Invert : analogOptions.analogAdc1Invert;
    stick.ema_option = useStick2 ? analogOptions.analog_smoothing2 : analogOptions.analog_smoothing;
    stick.ema_smoothing = useStick2 ? analogOptions.smoothing_factor2 : analogOptions.smoothing_factor;
    stick.error_rate = (useStick2 ? analogOptions.analog_error2 : analogOptions.analog_error) / 1000.0f;
    stick.in_deadzone = (useStick2 ? analogOptions.inner_deadzone2 : analogOptions.inner_deadzone) / 100.0f;
    stick.out_deadzone = (useStick2 ? analogOptions.outer_deadzone2 : analogOptions.outer_deadzone) / 100.0f;
    stick.auto_calibration = useStick2 ? analogOptions.auto_calibrate2 : analogOptions.auto_calibrate;
    stick.forced_circularity = useStick2 ? analogOptions.forced_circularity2 : analogOptions.forced_circularity;
    stick.joystick_center_x = useStick2 ? analogOptions.joystick_center_x2 : analogOptions.joystick_center_x;
    stick.joystick_center_y = useStick2 ? analogOptions.joystick_center_y2 : analogOptions.joystick_center_y;
    stick.x_min = useStick2 ? analogOptions.joystick_min_x2 : analogOptions.joystick_min_x;
    stick.x_max = useStick2 ? analogOptions.joystick_max_x2 : analogOptions.joystick_max_x;
    stick.y_min = useStick2 ? analogOptions.joystick_min_y2 : analogOptions.joystick_min_y;
    stick.y_max = useStick2 ? analogOptions.joystick_max_y2 : analogOptions.joystick_max_y;

    // Validate / normalize pins
    if (!isValidPin(stick.x_pin) || !isAdcPin(stick.x_pin)) stick.x_pin = (Pin_t)-1;
    if (!isValidPin(stick.y_pin) || !isAdcPin(stick.y_pin)) stick.y_pin = (Pin_t)-1;
    stick.x_pin_adc = stick.x_pin - STICK_MODE_SWITCHER_ADC_GPIO_OFFSET;
    stick.y_pin_adc = stick.y_pin - STICK_MODE_SWITCHER_ADC_GPIO_OFFSET;

    // Deadzone setup (same clamping as native Analog)
    stick.in_deadzone = std::clamp(stick.in_deadzone, ANALOG_MINIMUM, ANALOG_MAX - ANALOG_MIN_DEADZONE_BAND);
    stick.out_deadzone = std::clamp(stick.out_deadzone, stick.in_deadzone + ANALOG_MIN_DEADZONE_BAND, ANALOG_MAX);
    inverse_deadzone_span = 1.0f / (stick.out_deadzone - stick.in_deadzone);

    stick.x_value = ANALOG_CENTER;
    stick.y_value = ANALOG_CENTER;
    stick.xy_magnitude = 0.0f;
    stick.x_magnitude = 0.0f;
    stick.y_magnitude = 0.0f;
    stick.x_ema_initialized = false;
    stick.y_ema_initialized = false;
    stick.x_ema = ANALOG_CENTER;
    stick.y_ema = ANALOG_CENTER;

    const float strength = stick.ema_smoothing;
    stick.ema_option &= strength > 0.0f && strength <= 10.0f;
    stick.ema_smoothing = stick.ema_option ? powf(10.0f, -0.5f * strength) : 0.0f;

    if (stick.error_rate <= 0.0f || stick.error_rate > 1.0f) {
        stick.error_rate = 1.0f;
    }

    // Initialize ADC and auto-center (same sample averaging as native Analog)
    if (isValidPin(stick.x_pin)) {
        adc_gpio_init(stick.x_pin);
        if (stick.auto_calibration) {
            uint32_t sum = 0;
            adc_select_input(stick.x_pin_adc);
            for (int i = 0; i < 16; i++) sum += adc_read();
            stick.x_center = (sum + 8) / 16;
        } else {
            stick.x_center = stick.joystick_center_x;
        }
    }
    if (isValidPin(stick.y_pin)) {
        adc_gpio_init(stick.y_pin);
        if (stick.auto_calibration) {
            uint32_t sum = 0;
            adc_select_input(stick.y_pin_adc);
            for (int i = 0; i < 16; i++) sum += adc_read();
            stick.y_center = (sum + 8) / 16;
        } else {
            stick.y_center = stick.joystick_center_y;
        }
    }

    // Restore the persisted output mode (power-cycle memory). On boot the
    // firmware driver already matches the persisted input mode (XInput for the
    // analog modes, Keyboard for WASD), because the transition saved it.
    currentMode = options.savedMode;
    if (currentMode >= STICK_MODE_SWITCHER_MODE_COUNT) {
        currentMode = STICK_MODE_SWITCHER_DEFAULT_MODE;
    }

    // WASD keyboard-mapping override tracking
    wasdMappingApplied = false;
    wasdMappingTarget = nullptr;
    originalKeyDpadUp = 0;
    originalKeyDpadDown = 0;
    originalKeyDpadLeft = 0;
    originalKeyDpadRight = 0;
}

/**
 * @brief Edge-detect the mode button (debounced) and cycle the output mode.
 *
 * Each new mode also dictates the required USB input mode:
 *   - left / right analog -> XInput gamepad
 *   - WASD                -> Keyboard
 * If the required input mode differs from the current one, the add-on writes
 * it to gamepadOptions.inputMode, saves the whole config to flash (which also
 * persists the selected mode across power cycles), and reboots -- exactly the
 * same "save + reboot" sequence the web configurator uses for input mode
 * changes. Transitions between the two analog modes need no reboot.
 *
 * When leaving WASD mode the dpad is explicitly cleared so any held key (e.g.
 * W) is released and the user's original keyboard mapping is restored.
 */
void StickModeSwitcherAddon::handleModeButton(Gamepad *gamepad) {
    if (!isValidPin(modeButtonPin)) return;

    uint32_t now = getMillis();
    bool rawPressed = !gpio_get(modeButtonPin); // active low (pull-up)

    if (rawPressed != (bool)lastRawButton) {
        lastRawButton = (uint8_t)rawPressed;
        buttonChangeTime = now;
    }

    bool debouncedPressed = rawPressed &&
        (now - buttonChangeTime) >= STICK_MODE_SWITCHER_BUTTON_DEBOUNCE_MS;

    if (debouncedPressed && !buttonWasPressed) {
        buttonWasPressed = true; // consumed this press

        const uint8_t newMode = (currentMode + 1) % STICK_MODE_SWITCHER_MODE_COUNT;

        // Leaving WASD: release any held key + restore the original mapping
        if (currentMode == STICK_MODE_SWITCHER_MODE_WASD) {
            gamepad->state.dpad &= ~GAMEPAD_MASK_DPAD;
            applyWasdMapping(false);
        }

        // Which USB input mode does the new mode require?
        const InputMode requiredInputMode =
            (newMode == STICK_MODE_SWITCHER_MODE_WASD) ? INPUT_MODE_KEYBOARD
                                                       : INPUT_MODE_XINPUT;

        currentMode = newMode;
        if (currentMode == STICK_MODE_SWITCHER_MODE_WASD) {
            applyWasdMapping(true);
        }

        // Persist the mode itself (power-cycle memory)
        StickModeSwitcherOptions& myOptions =
            Storage::getInstance().getAddonOptions().stickModeSwitcherOptions;
        myOptions.savedMode = currentMode;

        if (DriverManager::getInstance().getInputMode() != requiredInputMode) {
            // USB descriptor swap: save the requested input mode + mode, then
            // reboot so the new driver (Keyboard / XInput) gets initialized.
            Storage::getInstance().getGamepadOptions().inputMode = requiredInputMode;
            Storage::getInstance().save(true);
            EventManager::getInstance().triggerEvent(
                new GPRestartEvent(System::BootMode::DEFAULT));
        } else {
            // Same USB type (analog <-> analog): no reboot needed
            Storage::getInstance().save(true);
        }
    } else if (!debouncedPressed) {
        buttonWasPressed = false;
    }
}

/**
 * @brief Override the active KeyboardMapping dpad keys with W/A/S/D while in
 * WASD mode, and restore the user's original values when leaving it.
 *
 * The KeyboardDriver reads the active keyboard mapping every frame, so the
 * WASD output applies immediately and is guaranteed regardless of the board's
 * default mapping (usually arrow keys).
 *
 * The four direction keys come from the add-on options (wasdKeyUp/Down/
 * Left/Right), configurable in the web configurator; they default to
 * W/A/S/D. A value of 0 (None) makes that direction send no key.
 */
void StickModeSwitcherAddon::applyWasdMapping(bool enable) {
    KeyboardMapping& mapping = Storage::getInstance().getActiveKeyboardMapping();

    if (enable) {
        // First time we take over this mapping object, remember the originals
        if (wasdMappingTarget != &mapping) {
            wasdMappingTarget = &mapping;
            originalKeyDpadUp = mapping.keyDpadUp;
            originalKeyDpadDown = mapping.keyDpadDown;
            originalKeyDpadLeft = mapping.keyDpadLeft;
            originalKeyDpadRight = mapping.keyDpadRight;
        }
        // Apply the user's custom key selection (defaults to W/A/S/D)
        const StickModeSwitcherOptions& myOptions =
            Storage::getInstance().getAddonOptions().stickModeSwitcherOptions;
        mapping.keyDpadUp = myOptions.wasdKeyUp;
        mapping.keyDpadDown = myOptions.wasdKeyDown;
        mapping.keyDpadLeft = myOptions.wasdKeyLeft;
        mapping.keyDpadRight = myOptions.wasdKeyRight;
        wasdMappingApplied = true;
    } else {
        // Only restore the object we actually took over (guards against a
        // profile switch changing the active mapping underneath us).
        if (wasdMappingTarget != nullptr && wasdMappingTarget == &mapping) {
            mapping.keyDpadUp = originalKeyDpadUp;
            mapping.keyDpadDown = originalKeyDpadDown;
            mapping.keyDpadLeft = originalKeyDpadLeft;
            mapping.keyDpadRight = originalKeyDpadRight;
        }
        wasdMappingTarget = nullptr;
        wasdMappingApplied = false;
    }
}

void __not_in_flash_func(StickModeSwitcherAddon::process)() {
    Gamepad *gamepad = Storage::getInstance().GetGamepad();

    handleModeButton(gamepad);

    // Keep the WASD mapping override in place if a profile switch changed the
    // active keyboard mapping object, or if the user changed the custom WASD
    // keys in the web configurator while we are already in WASD mode.
    if (currentMode == STICK_MODE_SWITCHER_MODE_WASD) {
        KeyboardMapping& activeMapping = Storage::getInstance().getActiveKeyboardMapping();
        const StickModeSwitcherOptions& myOptions =
            Storage::getInstance().getAddonOptions().stickModeSwitcherOptions;
        if (wasdMappingTarget != &activeMapping ||
            activeMapping.keyDpadUp != myOptions.wasdKeyUp ||
            activeMapping.keyDpadDown != myOptions.wasdKeyDown ||
            activeMapping.keyDpadLeft != myOptions.wasdKeyLeft ||
            activeMapping.keyDpadRight != myOptions.wasdKeyRight) {
            applyWasdMapping(true);
        }
    }

    uint32_t joystickMax = GAMEPAD_JOYSTICK_MAX;
    GPDriver *driver = DriverManager::getInstance().getDriver();
    if (driver != nullptr) {
        joystickMax = driver->GetJoystickMidValue() * 2; // mid must be 2x max
    }

    // Build the axis sample list (X first, then Y), mirroring Analog's order
    struct AxisSample { bool xAxis; Pin_t input; } samples[2];
    uint8_t sampleCount = 0;
    if (isValidPin(stick.x_pin)) samples[sampleCount++] = { true,  stick.x_pin_adc };
    if (isValidPin(stick.y_pin)) samples[sampleCount++] = { false, stick.y_pin_adc };

    if (sampleCount != 0) {
        adc_select_input(samples[0].input);
        hw_set_bits(&adc_hw->cs, ADC_CS_START_ONCE_BITS);
    }

    for (uint8_t sample = 0; sample < sampleCount; sample++) {
        while (!(adc_hw->cs & ADC_CS_READY_BITS)) tight_loop_contents();
        uint16_t adcValue = (uint16_t)adc_hw->result;

        if (sample + 1 < sampleCount) {
            adc_select_input(samples[sample + 1].input);
            hw_set_bits(&adc_hw->cs, ADC_CS_START_ONCE_BITS);
        }

        const AxisSample& axis = samples[sample];
        float value = axis.xAxis
            ? normalizePin(adcValue, stick.x_center, stick.x_min, stick.x_max)
            : normalizePin(adcValue, stick.y_center, stick.y_min, stick.y_max);

        if (axis.xAxis) {
            if (stick.analog_invert == InvertMode::INVERT_X || stick.analog_invert == InvertMode::INVERT_XY) value = ANALOG_MAX - value;
            if (stick.ema_option) {
                if (stick.x_ema_initialized) value = emaCalculation(value, stick.x_ema);
                stick.x_ema = value;
                stick.x_ema_initialized = true;
            }
            stick.x_value = value;
        } else {
            if (stick.analog_invert == InvertMode::INVERT_Y || stick.analog_invert == InvertMode::INVERT_XY) value = ANALOG_MAX - value;
            if (stick.ema_option) {
                if (stick.y_ema_initialized) value = emaCalculation(value, stick.y_ema);
                stick.y_ema = value;
                stick.y_ema_initialized = true;
            }
            stick.y_value = value;
        }
    }

    // Radial deadzone / forced circularity processing (identical to Analog)
    if (sampleCount != 0) {
        stick.xy_magnitude = magnitudeCalculation();
        if (stick.xy_magnitude <= stick.in_deadzone) {
            stick.x_value = ANALOG_CENTER;
            stick.y_value = ANALOG_CENTER;
        } else {
            radialDeadzone();
        }
    } else {
        stick.x_value = ANALOG_CENTER;
        stick.y_value = ANALOG_CENTER;
    }

    // Route the stick output according to the active mode
    switch (currentMode) {
        case STICK_MODE_SWITCHER_MODE_LEFT_ANALOG:
            writeAnalogOutput(gamepad, joystickMax, true);
            break;
        case STICK_MODE_SWITCHER_MODE_RIGHT_ANALOG:
            writeAnalogOutput(gamepad, joystickMax, false);
            break;
        case STICK_MODE_SWITCHER_MODE_WASD:
            writeWasdOutput(gamepad);
            break;
        default:
            break;
    }
}

void StickModeSwitcherAddon::writeAnalogOutput(Gamepad *gamepad, uint32_t joystickMax, bool leftStick) {
    // When the stick is centered, Gamepad::read() already reset the axes to
    // mid each frame, so there is nothing to write and nothing stale remains.
    if (stick.x_value == ANALOG_CENTER && stick.y_value == ANALOG_CENTER) {
        return;
    }

    uint16_t const clampedX = static_cast<uint16_t>(std::min(
        static_cast<uint32_t>(joystickMax * std::min(stick.x_value, 1.0f)), UINT32_C(0xFFFF)));
    uint16_t const clampedY = static_cast<uint16_t>(std::min(
        static_cast<uint32_t>(joystickMax * std::min(stick.y_value, 1.0f)), UINT32_C(0xFFFF)));

    if (leftStick) {
        gamepad->state.lx = clampedX;
        gamepad->state.ly = clampedY;
    } else {
        gamepad->state.rx = clampedX;
        gamepad->state.ry = clampedY;
    }
}

/**
 * @brief Edge-like WASD output: a stick deflection beyond the threshold
 * presses the key (it stays held in the HID report), and returning to center
 * removes it. The KeyboardDriver sends a fresh report whenever the key set
 * changes, so the host sees exactly one press/release pair per deflection.
 */
void StickModeSwitcherAddon::writeWasdOutput(Gamepad *gamepad) {
    uint8_t stickDpad = 0;
    if (stick.y_value < ANALOG_CENTER - STICK_MODE_SWITCHER_WASD_THRESHOLD) stickDpad |= GAMEPAD_MASK_UP;
    if (stick.y_value > ANALOG_CENTER + STICK_MODE_SWITCHER_WASD_THRESHOLD) stickDpad |= GAMEPAD_MASK_DOWN;
    if (stick.x_value < ANALOG_CENTER - STICK_MODE_SWITCHER_WASD_THRESHOLD) stickDpad |= GAMEPAD_MASK_LEFT;
    if (stick.x_value > ANALOG_CENTER + STICK_MODE_SWITCHER_WASD_THRESHOLD) stickDpad |= GAMEPAD_MASK_RIGHT;

    // The stick is the exclusive dpad source while in WASD mode
    gamepad->state.dpad = stickDpad;
}

float StickModeSwitcherAddon::normalizePin(uint16_t adc_value, uint32_t center, uint32_t minimum, uint32_t maximum) {
    // Only a valid calibration changes the raw ADC scaling
    if (minimum < center && center < maximum && maximum <= ADC_MAX) {
        const float delta = (float)adc_value - center;
        const float span = adc_value < center ? center - minimum : maximum - center;
        return std::clamp(ANALOG_CENTER + ANALOG_CENTER * (delta / span), ANALOG_MINIMUM, ANALOG_MAX);
    }
    return ((float)adc_value) / ADC_MAX;
}

float StickModeSwitcherAddon::emaCalculation(float ema_value, float ema_previous) {
    return ema_previous + stick.ema_smoothing * (ema_value - ema_previous);
}

float StickModeSwitcherAddon::magnitudeCalculation() {
    stick.x_magnitude = stick.x_value - ANALOG_CENTER;
    stick.y_magnitude = stick.y_value - ANALOG_CENTER;
    return stick.error_rate * std::sqrt((stick.x_magnitude * stick.x_magnitude) + (stick.y_magnitude * stick.y_magnitude));
}

void StickModeSwitcherAddon::radialDeadzone() {
    float scaling_factor = (stick.xy_magnitude - stick.in_deadzone) * inverse_deadzone_span;
    if (stick.forced_circularity == true) {
        scaling_factor = std::fmin(scaling_factor, ANALOG_CENTER * stick.error_rate);
    }
    scaling_factor /= stick.xy_magnitude;
    stick.x_value = (stick.x_magnitude * scaling_factor) + ANALOG_CENTER;
    stick.y_value = (stick.y_magnitude * scaling_factor) + ANALOG_CENTER;
    stick.x_value = std::clamp(stick.x_value, ANALOG_MINIMUM, ANALOG_MAX);
    stick.y_value = std::clamp(stick.y_value, ANALOG_MINIMUM, ANALOG_MAX);
}
