#include "tge_environment.hpp"

#include <gtest/gtest.h>

//////////////////////////////////////////////////////////////////////////
int main(int argc, char** argv)
{
	testing::InitGoogleTest(&argc, argv);
	testing::AddGlobalTestEnvironment(new Klip::Tests::CTgeEnvironment{});

	return RUN_ALL_TESTS();
}
