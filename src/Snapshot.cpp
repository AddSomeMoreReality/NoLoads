#include "Snapshot.h"

#include "MainThread.h"
#include "Settings.h"
#include "Trace.h"

namespace
{
	constexpr std::uint32_t kMaxBufferRead = 64;

	struct BufferEntry
	{
		std::uintptr_t cell;
		RE::FormID     id;
		std::uint32_t  state;
	};

	// H9: the buffer length is not confirmed, so a bad read must not crash the game.
	// SEH only, no objects with destructors in this function.
	std::int32_t ReadBuffer(RE::TESObjectCELL** a_buffer, std::uint32_t a_count, BufferEntry* a_out)
	{
		__try {
			for (std::uint32_t i = 0; i < a_count; ++i) {
				auto* cell = a_buffer[i];
				a_out[i].cell = reinterpret_cast<std::uintptr_t>(cell);
				a_out[i].id = cell ? cell->GetFormID() : 0;
				a_out[i].state = cell ? static_cast<std::uint32_t>(std::to_underlying(cell->cellState.get())) : 0;
			}
			return static_cast<std::int32_t>(a_count);
		} __except (1 /* EXCEPTION_EXECUTE_HANDLER */) {
			return -1;
		}
	}

	std::uint32_t IniUInt(std::string_view a_name)
	{
		auto* ini = RE::INISettingCollection::GetSingleton();
		auto* setting = ini ? ini->GetSetting(a_name) : nullptr;
		return setting ? setting->data.u : 0;
	}

	std::string BufferJson(RE::TESObjectCELL** a_buffer, std::uint32_t a_iniLength)
	{
		if (!a_buffer) {
			return "null";
		}
		const auto count = (std::min)(a_iniLength, kMaxBufferRead);  // parenthesized: min is a <windows.h> macro

		std::array<BufferEntry, kMaxBufferRead> entries{};
		const auto read = ReadBuffer(a_buffer, count, entries.data());
		if (read < 0) {
			return R"({"error":"access violation"})";
		}

		std::string json = "[";
		for (std::int32_t i = 0; i < read; ++i) {
			const auto& entry = entries[static_cast<std::size_t>(i)];
			if (i > 0) {
				json += ',';
			}
			json += entry.cell ?
			            Trace::Args{}.FormID("id", entry.id).Str("state", Trace::CellStateName(entry.state)).Json() :
			            "null";
		}
		return json + "]";
	}

	std::string GridJson(RE::GridCellArray* a_grid)
	{
		std::string json = "[";
		for (std::uint32_t i = 0; i < a_grid->length; ++i) {
			for (std::uint32_t j = 0; j < a_grid->length; ++j) {
				if (json.size() > 1) {
					json += ',';
				}
				auto*       cell = a_grid->GetCell(i, j);
				Trace::Args args;
				args.XY("ij", i, j).Cell("cell", cell);
				if (cell) {
					args.Str("state", Trace::CellStateName(Trace::CellState(cell)));
					if (const auto* loaded = cell->GetRuntimeData().loadedData) {
						args.Bool("refsFullyLoaded", loaded->refsFullyLoaded)
							.Int("queuedRefs", loaded->queuedRefCount)
							.Int("criticalQueuedRefs", loaded->criticalQueuedRefCount)
							.Int("queuedDistantRefs", loaded->queuedDistantRefCount);
					}
				}
				json += args.Json();
			}
		}
		return json + "]";
	}

	void TakeNow(std::string_view a_reason)
	{
		const auto& settings = Settings::Get();

		Trace::Args args;
		args.Str("reason", a_reason);

		if (auto* tes = RE::TES::GetSingleton()) {
			args.Cell("interiorCell", tes->interiorCell)
				.Form("world", tes->GetRuntimeData2().worldSpace)
				.XY("grid", tes->currentGridX, tes->currentGridY)
				.XY("queued", tes->currentQueuedX, tes->currentQueuedY)
				.XY("saveGrid", tes->saveGridX, tes->saveGridY);
			if (settings.snapshotGrid && tes->gridCells) {
				args.Raw("cells", GridJson(tes->gridCells));
			}
			if (settings.readCellBuffers) {
				args.Raw("interiorBuffer", BufferJson(tes->interiorBuffer, IniUInt("uInterior Cell Buffer:General")))
					.Raw("exteriorBuffer", BufferJson(tes->exteriorBuffer, IniUInt("uExterior Cell Buffer:General")));
			}
		}

		if (auto* loader = RE::ExteriorCellLoader::GetSingleton()) {
			RE::BSReadLockGuard guard{ loader->lock };
			args.Int("queuedCellLoads", static_cast<std::int64_t>(loader->queuedCellMap.size()));
		}

		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			const auto& runtime = player->GetPlayerRuntimeData();
			const auto& target = runtime.queuedTargetLoc;
			args.Pos("playerPos", player->GetPosition())
				.Cell("parentCell", player->GetParentCell())
				.Form("location", player->GetCurrentLocation())
				.Bool("isLoading", runtime.playerFlags.isLoading)
				.Bool("target_valid", target.isValid)
				.Form("target_world", target.world)
				.Cell("target_interior", target.interior)
				.Pos("target_pos", target.location);
		}

		Trace::Write("snapshot", args, nullptr, true);
	}
}

namespace Snapshot
{
	void Take(std::string_view a_reason)
	{
		if (Trace::IsMainThread()) {
			TakeNow(a_reason);
		} else {
			// No thread check inside the task: re-posting from it is what hung 0.3.0.
			MainThread::Post([reason = std::string{ a_reason }]() { TakeNow(reason); });
		}
	}
}
