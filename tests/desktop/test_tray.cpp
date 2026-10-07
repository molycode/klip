#include "bus/connection.hpp"
#include "bus/portal.hpp"
#include "desktop/fake_panel.hpp"
#include "desktop/tray.hpp"

#include <gtest/gtest.h>
#include <systemd/sd-bus.h>
#include <tge/testing/expected_log.hpp>
#include <tge/threading/mpsc_queue.hpp>

#include <cstdint>
#include <cstdlib>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

using namespace Klip;

namespace
{
constexpr char const* ItemPath{ "/StatusNotifierItem" };
constexpr char const* ItemInterface{ "org.kde.StatusNotifierItem" };
constexpr char const* MenuPath{ "/MenuBar" };
constexpr char const* MenuInterface{ "com.canonical.dbusmenu" };

Tests::CFakePanel gFakePanel;

struct SMenuItem final
{
	int32_t     id{ 0 };
	std::string label;
	std::string type;
};

Desktop::STrayImage MakeImage(uint8_t fill)
{
	return Desktop::STrayImage{ 2, 2, std::vector<uint8_t>(16, fill) };
}

class CTrayTest : public testing::Test
{
protected:

	static void SetUpTestSuite()
	{
		ASSERT_TRUE(Bus::gConnection.Initialize("klip-bus"));
		ASSERT_TRUE(gFakePanel.Initialize()) << "Cannot own org.kde.StatusNotifierWatcher on the test bus";
	}

	static void TearDownTestSuite()
	{
		gFakePanel.Terminate();
		Bus::gConnection.Terminate();
	}

	void SetUp() override
	{
		ASSERT_TRUE(InitializeTray());
		gFakePanel.Follow(m_service);
	}

	void TearDown() override
	{
		m_tray.Terminate();

		Desktop::SRequest request;

		while (m_requests.Dequeue(request))
		{
		}
	}

	bool InitializeTray()
	{
		return m_tray.Initialize(Desktop::STrayIcons{ MakeImage(0x11), MakeImage(0x22) },
		                         [this](Desktop::SRequest const& request) { m_requests.Enqueue(request); });
	}

	std::vector<Desktop::SRequest> TakeRequests()
	{
		std::vector<Desktop::SRequest> taken;
		Desktop::SRequest request;

		while (m_requests.Dequeue(request))
		{
			taken.push_back(request);
		}

		return taken;
	}

	// The posted change has gone out, and the panel has seen every signal sent before Klip's next answer.
	void Settle()
	{
		Bus::gConnection.Run([](sd_bus*) {});
		GetString(ItemPath, ItemInterface, "Id");
	}

	int Call(char const* pPath, char const* pInterface, char const* pMember, char const* pTypes, auto... arguments)
	{
		int result{ 0 };

		gFakePanel.GetConnection().Run([&](sd_bus* pBus) {
			sd_bus_error error{ SD_BUS_ERROR_NULL };

			result = sd_bus_call_method(pBus, m_service.c_str(), pPath, pInterface, pMember, &error, nullptr, pTypes,
			                            arguments...);
			sd_bus_error_free(&error);
		});

		return result;
	}

	std::string GetString(char const* pPath, char const* pInterface, char const* pName)
	{
		std::string value;

		gFakePanel.GetConnection().Run([&](sd_bus* pBus) {
			char* pValue{ nullptr };

			if (sd_bus_get_property_string(pBus, m_service.c_str(), pPath, pInterface, pName, nullptr, &pValue) >= 0)
			{
				value = pValue;
			}

			free(pValue);
		});

		return value;
	}

	// Each property's signature, or nothing when the call failed.
	std::map<std::string, std::string> GetAll(char const* pPath, char const* pInterface)
	{
		std::map<std::string, std::string> signatures;

		gFakePanel.GetConnection().Run([&](sd_bus* pBus) {
			sd_bus_message* pReply{ nullptr };

			if (sd_bus_call_method(pBus, m_service.c_str(), pPath, "org.freedesktop.DBus.Properties", "GetAll",
			                       nullptr, &pReply, "s", pInterface) >= 0)
			{
				Bus::ReadDict(pReply, [&signatures](std::string_view key, sd_bus_message* pEntry) {
					char type{ 0 };
					char const* pContents{ nullptr };

					if (sd_bus_message_peek_type(pEntry, &type, &pContents) > 0 && pContents != nullptr)
					{
						signatures[std::string{ key }] = pContents;
					}

					return false;
				});
			}

			sd_bus_message_unref(pReply);
		});

		return signatures;
	}

	Desktop::STrayImage GetIcon()
	{
		Desktop::STrayImage image;

		gFakePanel.GetConnection().Run([&](sd_bus* pBus) {
			sd_bus_message* pReply{ nullptr };
			void const* pPixels{ nullptr };
			size_t size{ 0 };

			if (sd_bus_get_property(pBus, m_service.c_str(), ItemPath, ItemInterface, "IconPixmap", nullptr, &pReply,
			                        "a(iiay)") >= 0
			    && sd_bus_message_enter_container(pReply, 'a', "(iiay)") > 0
			    && sd_bus_message_enter_container(pReply, 'r', "iiay") > 0
			    && sd_bus_message_read(pReply, "ii", &image.width, &image.height) >= 0
			    && sd_bus_message_read_array(pReply, 'y', &pPixels, &size) >= 0)
			{
				uint8_t const* pBytes{ static_cast<uint8_t const*>(pPixels) };
				image.pixels.assign(pBytes, pBytes + size);
			}

			sd_bus_message_unref(pReply);
		});

		return image;
	}

	std::string GetToolTipDetail()
	{
		std::string detail;

		gFakePanel.GetConnection().Run([&](sd_bus* pBus) {
			sd_bus_message* pReply{ nullptr };
			char const* pIconName{ nullptr };
			char const* pTitle{ nullptr };
			char const* pDetail{ nullptr };

			if (sd_bus_get_property(pBus, m_service.c_str(), ItemPath, ItemInterface, "ToolTip", nullptr, &pReply,
			                        "(sa(iiay)ss)") >= 0
			    && sd_bus_message_enter_container(pReply, 'r', "sa(iiay)ss") > 0
			    && sd_bus_message_read(pReply, "s", &pIconName) >= 0 && sd_bus_message_skip(pReply, "a(iiay)") >= 0
			    && sd_bus_message_read(pReply, "ss", &pTitle, &pDetail) >= 0)
			{
				detail = pDetail;
			}

			sd_bus_message_unref(pReply);
		});

		return detail;
	}

	std::vector<SMenuItem> GetLayout(int32_t depth)
	{
		std::vector<SMenuItem> children;

		gFakePanel.GetConnection().Run([&](sd_bus* pBus) {
			sd_bus_message* pReply{ nullptr };
			uint32_t revision{ 0 };

			bool const read{ sd_bus_call_method(pBus, m_service.c_str(), MenuPath, MenuInterface, "GetLayout", nullptr,
			                                    &pReply, "iias", 0, depth, 0) >= 0
			                 && sd_bus_message_read(pReply, "u", &revision) >= 0
			                 && sd_bus_message_enter_container(pReply, 'r', "ia{sv}av") > 0
			                 && sd_bus_message_skip(pReply, "ia{sv}") >= 0
			                 && sd_bus_message_enter_container(pReply, 'a', "v") > 0 };

			while (read && sd_bus_message_enter_container(pReply, 'v', "(ia{sv}av)") > 0)
			{
				SMenuItem item;

				sd_bus_message_enter_container(pReply, 'r', "ia{sv}av");
				sd_bus_message_read(pReply, "i", &item.id);
				Bus::ReadDict(pReply, [&item](std::string_view key, sd_bus_message* pEntry) {
					return (key == "label" && Bus::ReadString(pEntry, item.label))
					       || (key == "type" && Bus::ReadString(pEntry, item.type));
				});
				sd_bus_message_skip(pReply, "av");
				sd_bus_message_exit_container(pReply);
				sd_bus_message_exit_container(pReply);

				children.push_back(item);
			}

			sd_bus_message_unref(pReply);
		});

		return children;
	}

	Desktop::CTray                                m_tray;
	Tge::Threading::CMpscQueue<Desktop::SRequest> m_requests;
	std::string const                             m_service{ std::format("org.kde.StatusNotifierItem-{}-1", getpid()) };
};
} // namespace

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, RegistersWithThePanel)
{
	EXPECT_TRUE(m_tray.IsAvailable());
	EXPECT_EQ(gFakePanel.GetRegistered(), m_service);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, WithoutAPanelItIsNotAvailable)
{
	m_tray.Terminate();
	gFakePanel.Withdraw();

	{
		Tge::Testing::CExpectedLog expected{ "Desktop", 1, 0 };

		EXPECT_FALSE(InitializeTray());
	}

	EXPECT_EQ(GetString(ItemPath, ItemInterface, "Id"), "klip");
	ASSERT_TRUE(gFakePanel.Restore());
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, TerminateTakesTheItemOffTheBus)
{
	m_tray.Terminate();

	EXPECT_TRUE(GetString(ItemPath, ItemInterface, "Id").empty());
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, ItemPropertiesAllLoadAtOnce)
{
	std::map<std::string, std::string> const expected{
		{ "AttentionIconName", "s" }, { "Category", "s" },   { "IconName", "s" },           { "IconPixmap", "a(iiay)" },
		{ "Id", "s" },                { "ItemIsMenu", "b" }, { "Menu", "o" },                { "OverlayIconName", "s" },
		{ "Status", "s" },            { "Title", "s" },      { "ToolTip", "(sa(iiay)ss)" }, { "XAyatanaLabel", "s" },
		{ "XAyatanaLabelGuide", "s" }
	};

	EXPECT_EQ(GetAll(ItemPath, ItemInterface), expected);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, MenuPropertiesAllLoadAtOnce)
{
	std::map<std::string, std::string> const expected{
		{ "IconThemePath", "as" }, { "Status", "s" }, { "TextDirection", "s" }, { "Version", "u" }
	};

	EXPECT_EQ(GetAll(MenuPath, MenuInterface), expected);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, ServesTheIdleIcon)
{
	Desktop::STrayImage const icon{ GetIcon() };

	EXPECT_EQ(icon.width, 2);
	EXPECT_EQ(icon.height, 2);
	EXPECT_EQ(icon.pixels, std::vector<uint8_t>(16, 0x11));
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, ServesTheRecordingIconWhileRecording)
{
	m_tray.SetRecording(true);
	Settle();

	EXPECT_EQ(GetIcon().pixels, std::vector<uint8_t>(16, 0x22));
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, TitleSaysWhenItIsRecording)
{
	EXPECT_EQ(GetString(ItemPath, ItemInterface, "Title"), "Klip");

	m_tray.SetRecording(true);
	Settle();

	EXPECT_EQ(GetString(ItemPath, ItemInterface, "Title"), "Klip — recording");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, ToolTipCarriesTheDetail)
{
	m_tray.SetDetail("12 MiB");
	Settle();

	EXPECT_EQ(GetToolTipDetail(), "12 MiB");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, LabelIsServedWithItsGuide)
{
	m_tray.SetLabel("00:00:01");
	Settle();

	EXPECT_EQ(GetString(ItemPath, ItemInterface, "XAyatanaLabel"), "00:00:01");
	EXPECT_EQ(GetString(ItemPath, ItemInterface, "XAyatanaLabelGuide"), "00:00:00 · 999.9 GiB/h");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, RecordingIsAnnounced)
{
	m_tray.SetRecording(true);
	Settle();

	std::vector<Tests::SRecordedSignal> const signals{ gFakePanel.TakeSignals() };

	ASSERT_EQ(signals.size(), 4u);
	EXPECT_EQ(signals[0].member, "NewIcon");
	EXPECT_EQ(signals[1].member, "NewTitle");
	EXPECT_EQ(signals[2].member, "NewToolTip");
	EXPECT_EQ(signals[3].member, "ItemsPropertiesUpdated");
	EXPECT_EQ(signals[3].signature, "a(ia{sv})a(ias)");
	EXPECT_EQ(signals[3].toggleLabel, "Stop recording");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, NewLabelIsAnnounced)
{
	m_tray.SetLabel("00:00:02");
	Settle();

	std::vector<Tests::SRecordedSignal> const signals{ gFakePanel.TakeSignals() };

	ASSERT_EQ(signals.size(), 1u);
	EXPECT_EQ(signals[0].member, "XAyatanaNewLabel");
	EXPECT_EQ(signals[0].signature, "ss");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, UnchangedLabelIsNotAnnounced)
{
	m_tray.SetLabel("Klip");
	Settle();

	EXPECT_TRUE(gFakePanel.TakeSignals().empty());
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, LeftClickAsksToShow)
{
	EXPECT_GE(Call(ItemPath, ItemInterface, "Activate", "ii", 0, 0), 0);

	std::vector<Desktop::SRequest> const requests{ TakeRequests() };

	ASSERT_EQ(requests.size(), 1u);
	EXPECT_EQ(requests[0].kind, Desktop::ERequest::Show);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, MiddleClickAsksToToggle)
{
	EXPECT_GE(Call(ItemPath, ItemInterface, "SecondaryActivate", "ii", 0, 0), 0);

	std::vector<Desktop::SRequest> const requests{ TakeRequests() };

	ASSERT_EQ(requests.size(), 1u);
	EXPECT_EQ(requests[0].kind, Desktop::ERequest::Toggle);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, ActivationTokenComesBeforeTheShowItSanctions)
{
	EXPECT_GE(Call(ItemPath, ItemInterface, "ProvideXdgActivationToken", "s", "token-1"), 0);
	EXPECT_GE(Call(ItemPath, ItemInterface, "Activate", "ii", 0, 0), 0);

	std::vector<Desktop::SRequest> const requests{ TakeRequests() };

	ASSERT_EQ(requests.size(), 2u);
	EXPECT_EQ(requests[0].kind, Desktop::ERequest::ActivationToken);
	EXPECT_EQ(requests[0].token, "token-1");
	EXPECT_EQ(requests[1].kind, Desktop::ERequest::Show);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, ContextMenuAndScrollAreAnsweredAndIgnored)
{
	EXPECT_GE(Call(ItemPath, ItemInterface, "ContextMenu", "ii", 0, 0), 0);
	EXPECT_GE(Call(ItemPath, ItemInterface, "Scroll", "is", 1, "vertical"), 0);
	EXPECT_TRUE(TakeRequests().empty());
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, MenuClicksBecomeRequests)
{
	for (int32_t const id : { 1, 2, 4 })
	{
		EXPECT_GE(Call(MenuPath, MenuInterface, "Event", "isvu", id, "clicked", "i", 0, 0u), 0);
	}

	std::vector<Desktop::SRequest> const requests{ TakeRequests() };

	ASSERT_EQ(requests.size(), 3u);
	EXPECT_EQ(requests[0].kind, Desktop::ERequest::Toggle);
	EXPECT_EQ(requests[1].kind, Desktop::ERequest::Show);
	EXPECT_EQ(requests[2].kind, Desktop::ERequest::Quit);
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, OtherMenuEventsAreAnsweredAndIgnored)
{
	EXPECT_GE(Call(MenuPath, MenuInterface, "Event", "isvu", 1, "hovered", "i", 0, 0u), 0);
	EXPECT_TRUE(TakeRequests().empty());
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, LayoutListsTheFourItems)
{
	std::vector<SMenuItem> const items{ GetLayout(-1) };

	ASSERT_EQ(items.size(), 4u);
	EXPECT_EQ(items[0].id, 1);
	EXPECT_EQ(items[0].label, "Start recording");
	EXPECT_EQ(items[1].id, 2);
	EXPECT_EQ(items[1].label, "Show Klip");
	EXPECT_EQ(items[2].id, 3);
	EXPECT_EQ(items[2].type, "separator");
	EXPECT_EQ(items[3].id, 4);
	EXPECT_EQ(items[3].label, "Quit");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CTrayTest, ShallowLayoutHasNoItems)
{
	EXPECT_TRUE(GetLayout(0).empty());
}
