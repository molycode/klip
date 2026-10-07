#pragma once

#include <gtest/gtest.h>

namespace Klip::Tests
{
// tge-core comes up once around the whole suite, as it does around the application.
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
