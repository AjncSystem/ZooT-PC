// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cmath>
#include <mutex>

#include "input_common/helpers/combo_input.h"

namespace InputCommon {
namespace {

/// Whether a child's status reads as pressed, whatever kind of input it is.
bool Pressed(const Common::Input::CallbackStatus& status) {
    using Common::Input::InputType;
    switch (status.type) {
    case InputType::Button:
        return status.button_status.value != status.button_status.inverted;
    case InputType::Trigger:
        return status.trigger_status.pressed.value ||
               status.trigger_status.analog.raw_value >
                   std::max(0.5f, status.trigger_status.analog.properties.threshold);
    case InputType::Analog: {
        const auto& a = status.analog_status;
        const float v = a.properties.inverted ? -a.raw_value : a.raw_value;
        return v - a.properties.offset > std::max(0.1f, a.properties.threshold);
    }
    default:
        return false;
    }
}

/// How far a child's stick is pushed past its own deadzone (0 when it is at rest).
float Push(const Common::Input::CallbackStatus& status) {
    if (status.type != Common::Input::InputType::Stick) {
        return 0.0f;
    }
    const auto& s = status.stick_status;
    const float x = s.x.raw_value - s.x.properties.offset;
    const float y = s.y.raw_value - s.y.properties.offset;
    const float length = std::sqrt(x * x + y * y);
    const float deadzone = std::max(s.x.properties.deadzone, 0.02f);
    return length > deadzone ? length : 0.0f;
}

class ComboDevice final : public Common::Input::InputDevice {
public:
    using Device = std::unique_ptr<Common::Input::InputDevice>;

    ComboDevice(Device a, Device b) : children{std::move(a), std::move(b)} {
        for (std::size_t i = 0; i < children.size(); ++i) {
            children[i]->SetCallback({
                .on_change =
                    [this, i](const Common::Input::CallbackStatus& status) {
                        Update(i, status);
                    },
            });
        }
    }

    void ForceUpdate() override {
        for (auto& child : children) {
            child->ForceUpdate();
        }
    }

private:
    void Update(std::size_t index, const Common::Input::CallbackStatus& status) {
        Common::Input::CallbackStatus out;
        {
            std::scoped_lock lock{mutex};
            last[index] = status;
            const auto& a = last[0];
            const auto& b = last[1];
            if (a.type == Common::Input::InputType::Stick ||
                b.type == Common::Input::InputType::Stick) {
                // The stick pushed further wins; at rest, the controller's (centered).
                const float pa = Push(a), pb = Push(b);
                out = pb > pa ? b : (a.type == Common::Input::InputType::Stick ? a : b);
            } else if (a.type == Common::Input::InputType::Trigger &&
                       b.type != Common::Input::InputType::Trigger) {
                // A controller trigger keeps its analog value; the key presses it fully.
                out = a;
                if (Pressed(b)) {
                    out.trigger_status.pressed.value = true;
                    out.trigger_status.analog.raw_value = 1.0f;
                }
            } else {
                out.type = Common::Input::InputType::Button;
                out.button_status.value = Pressed(a) || Pressed(b);
            }
        }
        TriggerOnChange(out);
    }

    std::array<Device, 2> children;
    std::array<Common::Input::CallbackStatus, 2> last{};
    std::mutex mutex;
};

} // Anonymous namespace

std::unique_ptr<Common::Input::InputDevice> ComboInput::Create(
    const Common::ParamPackage& params) {
    const std::string null_engine = Common::ParamPackage{{"engine", "null"}}.Serialize();
    auto a = Common::Input::CreateInputDeviceFromString(params.Get("a", null_engine));
    auto b = Common::Input::CreateInputDeviceFromString(params.Get("b", null_engine));
    return std::make_unique<ComboDevice>(std::move(a), std::move(b));
}

Common::ParamPackage ComboPrimary(const Common::ParamPackage& params) {
    if (params.Get("engine", "") != "combo") {
        return params;
    }
    return Common::ParamPackage{params.Get("a", "")};
}

} // namespace InputCommon
