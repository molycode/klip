#include "json/json.hpp"

#include "log.hpp"

#include <cstdlib>

namespace Klip::Json
{
//////////////////////////////////////////////////////////////////////////
// Without exceptions nlohmann aborts on any unchecked read; this logs why first.
void AbortOnJsonError(char const* pWhat)
{
	gLog.Error("nlohmann/json failed on a value that was not checked first: {}", pWhat);
	std::abort();
}
} // namespace Klip::Json
