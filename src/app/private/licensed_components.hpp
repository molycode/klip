#pragma once

#include "licensed_component.hpp"

#include <span>

namespace Klip
{
std::span<SLicensedComponent const> GetLicensedComponents();
} // namespace Klip
