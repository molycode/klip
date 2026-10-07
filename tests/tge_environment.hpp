#pragma once

#include <gtest/gtest.h>

namespace Klip::Tests
{
class CTgeEnvironment final : public testing::Environment
{
public:

	CTgeEnvironment() = default;
	~CTgeEnvironment() override = default;

	// testing::Environment
	void SetUp() override;
	void TearDown() override;
	// ~testing::Environment
};
} // namespace Klip::Tests
