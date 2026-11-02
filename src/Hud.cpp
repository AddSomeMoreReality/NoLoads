#include "Hud.h"

#include "Settings.h"
#include "Trace.h"

namespace
{
	constexpr std::int64_t kGapUs = 2'000'000;  // the game shows a notification for about two seconds
	constexpr std::size_t  kMaxWaiting = 8;

	struct Message
	{
		std::string channel;
		std::string text;
	};

	std::mutex          g_lock;
	std::deque<Message> g_waiting;
	std::int64_t        g_lastShownUs{ -kGapUs };
}

namespace Hud
{
	void Show(std::string_view a_channel, std::string a_text)
	{
		if (!Settings::Get().hotkeyMessages) {
			return;
		}
		const std::scoped_lock lock{ g_lock };
		const auto             it = std::ranges::find(g_waiting, a_channel, &Message::channel);
		if (it != g_waiting.end()) {
			it->text = std::move(a_text);
			return;
		}
		g_waiting.push_back({ std::string{ a_channel }, std::move(a_text) });
		if (g_waiting.size() > kMaxWaiting) {
			g_waiting.pop_front();
		}
	}

	void Tick()
	{
		std::string text;
		{
			const std::scoped_lock lock{ g_lock };
			const auto             now = Trace::NowUs();
			if (g_waiting.empty() || now - g_lastShownUs < kGapUs) {
				return;
			}
			text = std::move(g_waiting.front().text);
			g_waiting.pop_front();
			g_lastShownUs = now;
		}
		RE::SendHUDMessage::ShowHUDMessage(text.c_str(), nullptr, false);
	}

	std::string CellLabel(RE::TESObjectCELL* a_cell)
	{
		if (!a_cell) {
			return "?";
		}
		const char* name = a_cell->GetName();
		const char* edid = a_cell->GetFormEditorID();
		std::string label = (name && *name) ? name : (edid && *edid) ? edid : std::format("{:08X}", a_cell->GetFormID());
		if (a_cell->IsExteriorCell()) {
			if (const auto* coordinates = a_cell->GetCoordinates()) {
				label += std::format(" ({},{})", coordinates->cellX, coordinates->cellY);
			}
		}
		return label;
	}
}
