#pragma once

namespace dsk {

class VisibleRefreshGate {
public:
	void markDirty() { dirty_ = true; }

	bool takeIfVisible(bool visible)
	{
		if (!visible || !dirty_)
			return false;
		dirty_ = false;
		return true;
	}

	bool isDirty() const { return dirty_; }

private:
	bool dirty_ = true;
};

} // namespace dsk
