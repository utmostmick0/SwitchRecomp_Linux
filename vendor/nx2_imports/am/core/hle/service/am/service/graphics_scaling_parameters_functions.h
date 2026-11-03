// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file graphics_scaling_parameters_functions.h
 * @brief Graphics scaling and resolution parameter management.
 *
 * Manages dynamic resolution scaling, aspect ratios, and display scaling factors.
 */


#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

struct Applet;

struct GraphicsScalingParameters {
    u32 graphics_scale;
    u32 screen_ratio_numerator;
    u32 screen_ratio_denominator;
};
static_assert(sizeof(GraphicsScalingParameters) == 12);

class IGraphicsScalingParametersFunctions final
    : public ServiceFramework<IGraphicsScalingParametersFunctions> {
public:
    explicit IGraphicsScalingParametersFunctions(Core::System& system);
    ~IGraphicsScalingParametersFunctions() override;

private:
    Result GetGraphicsScalingParameters(Out<GraphicsScalingParameters> out_parameters);

};

} // namespace Service::AM
