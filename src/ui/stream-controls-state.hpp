#pragma once

#include "core/output-runtime-status.hpp"
#include "core/output-target.hpp"
#include "core/twitch-dual-format.hpp"

namespace dsk {

struct AllControlState {
	bool stopMode = false;
	bool enabled = false;
};

inline bool targetCanStartWithAll(const OutputTarget &target, const TargetRuntimeStatus &runtime,
				  bool suppressIndependentTwitch = false)
{
	if (shouldSuppressIndependentTwitchTarget(target, suppressIndependentTwitch))
		return false;
	const bool running = target.state == TargetState::Live || target.state == TargetState::Starting ||
			     runtimeTransportIsRunning(runtime);
	const bool busy = target.state == TargetState::Starting || target.state == TargetState::Stopping ||
			  runtimeTransportIsBusy(runtime);
	return target.enabled && target.startWithAll && !running && !busy;
}

inline bool targetBlocksStartAll(const OutputTarget &target, const TargetRuntimeStatus &runtime,
				 bool suppressIndependentTwitch = false)
{
	if (shouldSuppressIndependentTwitchTarget(target, suppressIndependentTwitch))
		return false;
	const bool busy = target.state == TargetState::Starting || target.state == TargetState::Stopping ||
			  runtimeTransportIsBusy(runtime);
	return target.enabled && target.startWithAll && busy;
}

inline bool obsNativeCanStartWithAll(bool available, bool active, bool transitioning)
{
	return available && !active && !transitioning;
}

inline bool obsNativeServiceConfigured(bool hasServiceName, bool hasAlternateName, bool hasServer, bool hasType)
{
	return hasServiceName || hasAlternateName || hasServer || hasType;
}

inline bool obsNativeRowAvailable(bool probeReady, bool serviceConfigured, bool active, bool transitioning)
{
	return active || transitioning || (probeReady && serviceConfigured);
}

inline AllControlState allControlState(int eligibleStartCount, bool startBlocked, bool startObsNative,
				       bool obsNativeActive, bool obsNativeTransitioning,
				       bool obsNativeExpectedActive, bool hasRunningTarget, bool hasStoppingTarget)
{
	const bool startingObsNative = obsNativeTransitioning && obsNativeExpectedActive;
	const bool stopMode = obsNativeActive || startingObsNative || hasRunningTarget || hasStoppingTarget;
	if (stopMode) {
		const bool canStop = !startingObsNative && (obsNativeActive || hasRunningTarget);
		return {true, canStop};
	}
	return {false, !startBlocked && (eligibleStartCount > 0 || startObsNative)};
}

} // namespace dsk
