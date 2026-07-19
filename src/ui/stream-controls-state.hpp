#pragma once

#include "core/output-runtime-status.hpp"
#include "core/output-target.hpp"

namespace dsk {

inline bool targetCanStartWithAll(const OutputTarget &target, const TargetRuntimeStatus &runtime)
{
	const bool running = target.state == TargetState::Live || target.state == TargetState::Starting ||
			     runtimeTransportIsRunning(runtime);
	const bool busy = target.state == TargetState::Starting || target.state == TargetState::Stopping ||
			  runtimeTransportIsBusy(runtime);
	return target.enabled && target.startWithAll && !running && !busy;
}

inline bool targetBlocksStartAll(const OutputTarget &target, const TargetRuntimeStatus &runtime)
{
	const bool busy = target.state == TargetState::Starting || target.state == TargetState::Stopping ||
			  runtimeTransportIsBusy(runtime);
	return target.enabled && target.startWithAll && busy;
}

inline bool obsNativeCanStartWithAll(bool available, bool active, bool transitioning)
{
	return available && !active && !transitioning;
}

} // namespace dsk
