#include "licensed_components.hpp"

#include "embedded_licences.hpp"

#include <array>

namespace Klip
{
//////////////////////////////////////////////////////////////////////////
std::span<SLicensedComponent const> GetLicensedComponents()
{
	static std::array const Components{
		SLicensedComponent{ "Klip", "MIT License", Embedded::KlipLicence },
		SLicensedComponent{ "Dear ImGui", "MIT License", Embedded::ImGuiLicence },
		SLicensedComponent{ "SDL", "zlib License", Embedded::SdlLicence },
		SLicensedComponent{ "SDL's bundled code", "BSD 3-Clause, X11 and MIT licences", Embedded::SdlBundledLicences },
		SLicensedComponent{ "SDL's Wayland protocols", "MIT and X11 licences", Embedded::SdlWaylandLicences },
		SLicensedComponent{ "JSON for Modern C++", "MIT License", Embedded::JsonLicence },
		SLicensedComponent{ "JSON's bundled code", "MIT, CC0 1.0 and Apache 2.0 licences", Embedded::JsonBundledLicences },
		SLicensedComponent{ "FFmpeg", "GNU LGPL 2.1 or later", Embedded::FfmpegLicence },
		SLicensedComponent{ "tge-core", "MIT License", Embedded::TgeCoreLicence },
		SLicensedComponent{ "rpmalloc", "Zero-Clause BSD", Embedded::RpmallocLicence },
		SLicensedComponent{ "Roboto", "Apache License 2.0", Embedded::RobotoLicence },
		SLicensedComponent{ "Font Awesome Free", "SIL Open Font License 1.1", Embedded::FontAwesomeLicence }
	};

	return Components;
}
} // namespace Klip
