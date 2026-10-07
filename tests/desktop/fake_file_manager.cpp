#include "desktop/fake_file_manager.hpp"

#include <cstdlib>

namespace Klip::Tests
{
namespace
{
constexpr char const* Service{ "org.freedesktop.FileManager1" };
constexpr char const* ObjectPath{ "/org/freedesktop/FileManager1" };

int ShowItems(sd_bus_message* pCall, void* pFileManager, sd_bus_error*)
{
	return static_cast<CFakeFileManager*>(pFileManager)->OnShowItems(pCall);
}

sd_bus_vtable const Vtable[]{
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("ShowItems", "ass", "", ShowItems, 0),
	SD_BUS_VTABLE_END
};
} // namespace

//////////////////////////////////////////////////////////////////////////
bool CFakeFileManager::Initialize()
{
	bool initialized{ false };

	if (m_connection.Initialize("fake-files"))
	{
		m_connection.Run([this, &initialized](sd_bus* pBus) {
			initialized = sd_bus_add_object_vtable(pBus, &m_pSlot, ObjectPath, Service, Vtable, this) >= 0 &&
			              sd_bus_request_name(pBus, Service, 0) >= 0;
		});
	}

	return initialized;
}

//////////////////////////////////////////////////////////////////////////
void CFakeFileManager::Terminate()
{
	m_connection.Run([this](sd_bus* pBus) {
		sd_bus_release_name(pBus, Service);

		m_pSlot = sd_bus_slot_unref(m_pSlot);
	});

	m_connection.Terminate();
}

//////////////////////////////////////////////////////////////////////////
std::vector<std::string> CFakeFileManager::TakeShown()
{
	std::vector<std::string> taken;

	m_connection.Run([this, &taken](sd_bus*) { taken.swap(m_shown); });

	return taken;
}

//////////////////////////////////////////////////////////////////////////
int CFakeFileManager::OnShowItems(sd_bus_message* pCall)
{
	char** ppUris{ nullptr };
	int result{ sd_bus_message_read_strv(pCall, &ppUris) };

	if (result >= 0)
	{
		for (char** ppUri{ ppUris }; *ppUri != nullptr; ++ppUri)
		{
			m_shown.emplace_back(*ppUri);
			std::free(*ppUri);
		}

		std::free(ppUris);
		result = sd_bus_reply_method_return(pCall, "");
	}

	return result;
}
} // namespace Klip::Tests
