// SPDX-License-Identifier: GPL-3.0-or-later
// ZooT-PC: one console input fed by two devices at once, so the controller and the keyboard and
// mouse work together (as in the SotC-PC port). Params: engine:combo,a:<controller>,b:<keyboard
// or mouse>. A button is pressed while either is; a stick follows the one pushed further.

#pragma once

#include "common/input.h"
#include "common/param_package.h"

namespace InputCommon {

class ComboInput final : public Common::Input::Factory<Common::Input::InputDevice> {
public:
    std::unique_ptr<Common::Input::InputDevice> Create(const Common::ParamPackage& params) override;
};

/// The controller half of a combo param (the param itself when it is not a combo): what the
/// controller's outputs (vibration), battery and colors come from.
Common::ParamPackage ComboPrimary(const Common::ParamPackage& params);

} // namespace InputCommon
