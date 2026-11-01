#include "Detectors.h"

#include "Hud.h"
#include "Settings.h"
#include "Trace.h"

namespace
{
	constexpr std::uint32_t kAttached = 7;            // TESObjectCELL::CellState
	constexpr float         kUnitsPerMeter = 70.0f;   // one game unit is about 1.43 cm
	constexpr std::uint32_t kMaxSplitVertices = 20'000;  // larger shapes are reported by their bound
	constexpr std::size_t   kMaxPiecesPerShape = 64;
	constexpr float         kMaxWindowSize = 1500.0f;  // units; a larger piece is not one window
	constexpr float         kLookRadius = 3000.0f;     // F6: references around the camera checked against the view ray
	constexpr std::size_t   kMaxLookHits = 12;
	constexpr int           kMaxLookThrough = 4;       // F6: hidden references the ray may pass through
	constexpr float         kLookThroughStep = 5.0f;   // units the ray restarts behind a hidden reference
	constexpr std::size_t   kMaxNewRefsPerScan = 200;   // bounds the cost of one window scan
	constexpr std::size_t   kMaxTracedDoors = 40;
	constexpr std::size_t   kMaxTracedWindows = 20;

	float Degrees(float a_radians)
	{
		return a_radians * 57.2957795f;
	}

	// Heading of a horizontal direction: 0 = north (+Y), 90 = east (+X), as the game's angle Z.
	float YawDegrees(const RE::NiPoint3& a_direction)
	{
		auto yaw = Degrees(std::atan2(a_direction.x, a_direction.y));
		return yaw < 0.0f ? yaw + 360.0f : yaw;
	}

	RE::NiPoint3 ToDegrees(const RE::NiPoint3& a_radians)
	{
		return { Degrees(a_radians.x), Degrees(a_radians.y), Degrees(a_radians.z) };
	}

	std::string Lower(std::string_view a_str)
	{
		std::string out{ a_str };
		std::ranges::transform(out, out.begin(), [](char a_c) { return (a_c >= 'A' && a_c <= 'Z') ? static_cast<char>(a_c - 'A' + 'a') : a_c; });
		return out;
	}

	std::vector<std::string> SplitList(std::string_view a_list)
	{
		std::vector<std::string> out;
		for (const auto part : std::views::split(a_list, ',')) {
			auto item = Lower(std::string_view{ part.begin(), part.end() });
			std::erase(item, ' ');
			if (!item.empty()) {
				out.push_back(std::move(item));
			}
		}
		return out;
	}

	bool IsWindowPath(std::string_view a_path)
	{
		static const auto keywords = SplitList(Settings::Get().windowKeywords);
		static const auto excluded = SplitList(Settings::Get().windowExclude);
		if (a_path.empty()) {
			return false;
		}
		const auto path = Lower(a_path);
		const auto has = [&](const std::string& a_word) { return path.find(a_word) != std::string::npos; };
		return std::ranges::any_of(keywords, has) && std::ranges::none_of(excluded, has);
	}

	std::string FileName(std::string_view a_path)
	{
		const auto slash = a_path.find_last_of("\\/");
		return Lower(slash == std::string_view::npos ? a_path : a_path.substr(slash + 1));
	}

	// The cells whose references are in the scene: the player's interior, or the attached exterior grid.
	std::vector<RE::TESObjectCELL*> LoadedCells()
	{
		std::vector<RE::TESObjectCELL*> cells;
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* playerCell = player ? player->GetParentCell() : nullptr;
		if (!playerCell) {
			return cells;
		}
		if (playerCell->IsInteriorCell()) {
			cells.push_back(playerCell);
			return cells;
		}
		auto* tes = RE::TES::GetSingleton();
		if (auto* grid = tes ? tes->gridCells : nullptr) {
			for (std::uint32_t i = 0; i < grid->length; ++i) {
				for (std::uint32_t j = 0; j < grid->length; ++j) {
					auto* cell = grid->GetCell(i, j);
					if (cell && Trace::CellState(cell) == kAttached) {
						cells.push_back(cell);
					}
				}
			}
		}
		if (std::ranges::find(cells, playerCell) == cells.end()) {
			cells.push_back(playerCell);
		}
		return cells;
	}

	// ------------------------------------------------------------------
	// Doors
	// ------------------------------------------------------------------
	std::vector<Detectors::Door> g_doors;  // sorted by distance
	std::vector<RE::FormID>      g_tracedDoors;
	RE::FormID                   g_nearestDoor{ 0 };
	std::int64_t                 g_nextDoorsUs{ 0 };

	void ScanDoors(const RE::NiPoint3& a_playerPos)
	{
		std::vector<Detectors::Door> doors;
		for (auto* cell : LoadedCells()) {
			cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
				const auto* teleport = a_ref ? a_ref->extraList.GetByType<RE::ExtraTeleport>() : nullptr;
				if (!teleport || !teleport->teleportData || a_ref->IsDisabled()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				Detectors::Door door;
				door.ref = a_ref->GetHandle();
				door.id = a_ref->GetFormID();
				door.pos = a_ref->GetPosition();
				door.angle = a_ref->GetAngle();
				door.linked = teleport->teleportData->linkedDoor;
				if (const auto linked = door.linked.get()) {
					if (auto* destination = linked->GetParentCell()) {
						door.destCell = destination->GetFormID();
						door.destInterior = destination->IsInteriorCell();
					}
				}
				door.distance = door.pos.GetDistance(a_playerPos);
				doors.push_back(door);
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
		std::ranges::sort(doors, {}, &Detectors::Door::distance);
		g_doors = std::move(doors);
	}

	std::string DoorJson(const Detectors::Door& a_door)
	{
		const auto ref = a_door.ref.get();
		const auto linked = a_door.linked.get();
		auto*      base = ref ? ref->GetBaseObject() : nullptr;
		auto*      doorForm = base ? base->As<RE::TESObjectDOOR>() : nullptr;
		const auto* teleport = ref ? ref->extraList.GetByType<RE::ExtraTeleport>() : nullptr;
		const auto* data = teleport ? teleport->teleportData : nullptr;
		auto*      destination = RE::TESForm::LookupByID<RE::TESObjectCELL>(a_door.destCell);

		Trace::Args args;
		args.Form("ref", ref.get())
			.Str("base", base ? base->GetName() : "")
			.Int("baseType", base ? static_cast<std::int64_t>(std::to_underlying(base->GetFormType())) : -1)
			.Pos("pos", a_door.pos)
			.Pos("angleDeg", ToDegrees(a_door.angle))
			.Num("dist", a_door.distance)
			.Bool("automatic", doorForm && doorForm->flags.any(RE::TESObjectDOOR::Flag::kAutomatic))
			.Bool("locked", ref && ref->IsLocked())
			.Cell("destCell", destination)
			.Str("destState", Trace::CellStateName(Trace::CellState(destination)));
		if (ref) {
			if (const auto* node = ref->Get3D()) {
				// Local +Y of the door mesh in the world: the side the mesh faces.
				const RE::NiPoint3 forward{ node->world.rotate.entry[0][1], node->world.rotate.entry[1][1], node->world.rotate.entry[2][1] };
				args.Num("meshYawDeg", YawDegrees(forward));
			}
		}
		if (data) {
			args.Pos("marker", data->position)
				.Pos("markerRotDeg", ToDegrees(data->rotation))
				.Int("teleportFlags", data->flags);
		}
		if (linked) {
			args.Form("linked", linked.get())
				.Pos("linkedPos", linked->GetPosition())
				.Pos("linkedAngleDeg", ToDegrees(linked->GetAngle()));
		}
		return args.Json();
	}

	void TraceDoors(std::string_view a_reason)
	{
		std::string doors;
		for (std::size_t i = 0; i < g_doors.size() && i < kMaxTracedDoors; ++i) {
			doors += (i ? "," : "") + DoorJson(g_doors[i]);
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		Trace::Write("detector.doors", Trace::Args{}
										   .Str("reason", a_reason)
										   .Cell("playerCell", player ? player->GetParentCell() : nullptr)
										   .Pos("playerPos", player ? player->GetPosition() : RE::NiPoint3{})
										   .Int("count", static_cast<std::int64_t>(g_doors.size()))
										   .Raw("doors", "[" + doors + "]"));
	}

	std::string DoorText(const Detectors::Door& a_door)
	{
		auto* destination = RE::TESForm::LookupByID<RE::TESObjectCELL>(a_door.destCell);
		return std::format("{:.0f} m -> {} ({})", a_door.distance / kUnitsPerMeter, Hud::CellLabel(destination),
			Trace::CellStateName(Trace::CellState(destination)));
	}

	void UpdateDoors(const RE::NiPoint3& a_playerPos)
	{
		ScanDoors(a_playerPos);

		std::vector<RE::FormID> nearest;
		for (std::size_t i = 0; i < g_doors.size() && i < Settings::Get().nearestCount; ++i) {
			nearest.push_back(g_doors[i].id);
		}
		if (nearest != g_tracedDoors) {
			g_tracedDoors = nearest;
			TraceDoors("changed");
		}
		const auto nearestId = g_doors.empty() ? 0 : g_doors.front().id;
		if (nearestId != g_nearestDoor) {
			g_nearestDoor = nearestId;
			if (nearestId && Settings::Get().detectorsNotify) {
				Hud::Show("door", "CLR: door " + DoorText(g_doors.front()));
			}
		}
	}

	// ------------------------------------------------------------------
	// Windows
	// ------------------------------------------------------------------
	struct Window
	{
		RE::FormID    ref{ 0 };
		std::string   texture;  // file name of the diffuse texture, or of the model
		RE::NiPoint3  center;   // world
		RE::NiPoint3  normal;   // world, unit; zero when unknown
		float         size{ 0.0f };  // largest extent, units
		std::uint32_t triangles{ 0 };
		bool          piece{ false };  // a connected piece of the shape, not the whole shape's bound
	};

	struct WindowStats
	{
		std::uint64_t refsScanned{ 0 };
		std::uint64_t shapes{ 0 };
		std::uint64_t shapesSplit{ 0 };     // vertices read from memory
		std::uint64_t shapesNoVertices{ 0 };  // no CPU copy of the vertices: bound only
		std::uint64_t pieces{ 0 };
		std::uint64_t piecesRejected{ 0 };  // not finite or larger than kMaxWindowSize
	};

	std::vector<std::string>                             g_rawSamples;  // raw vertex bytes of the first window shapes
	std::unordered_map<RE::FormID, std::vector<Window>> g_windowCache;  // per reference; statics do not move
	const void*                                          g_windowSpace{ nullptr };  // interior cell or worldspace
	WindowStats                                          g_windowStats;
	std::vector<std::pair<RE::FormID, std::size_t>>      g_tracedWindows;
	std::pair<RE::FormID, std::size_t>                   g_nearestWindow{ 0, 0 };
	std::int64_t                                         g_nextWindowsUs{ 0 };

	std::string_view DiffuseTexture(RE::BSGeometry* a_geometry)
	{
		auto* property = a_geometry->GetGeometryRuntimeData().shaderProperty.get();
		if (auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(property)) {
			auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(lighting->material);
			if (material && material->textureSet) {
				if (const char* path = material->textureSet->GetTexturePath(RE::BSTextureSet::Textures::kDiffuse)) {
					return path;
				}
			}
		} else if (auto* effect = netimmerse_cast<RE::BSEffectShaderProperty*>(property)) {
			if (auto* material = static_cast<RE::BSEffectShaderMaterial*>(effect->material)) {
				return material->sourceTexturePath.c_str();
			}
		}
		return {};
	}

	// SEH only, no objects with destructors: the renderer's CPU copy may be released under us.
	bool SafeCopy(void* a_dst, const void* a_src, std::size_t a_size)
	{
		__try {
			std::memcpy(a_dst, a_src, a_size);
			return true;
		} __except (1 /* EXCEPTION_EXECUTE_HANDLER */) {
			return false;
		}
	}

	float HalfToFloat(std::uint16_t a_half)
	{
		const std::uint32_t sign = static_cast<std::uint32_t>(a_half & 0x8000u) << 16;
		std::uint32_t       exponent = (a_half >> 10) & 0x1Fu;
		std::uint32_t       mantissa = a_half & 0x3FFu;
		std::uint32_t       bits;
		if (exponent == 0) {
			if (mantissa == 0) {
				bits = sign;
			} else {
				exponent = 127 - 15 + 1;
				while ((mantissa & 0x400u) == 0) {
					mantissa <<= 1;
					--exponent;
				}
				bits = sign | (exponent << 23) | ((mantissa & 0x3FFu) << 13);
			}
		} else if (exponent == 31) {
			bits = sign | 0x7F800000u | (mantissa << 13);
		} else {
			bits = sign | ((exponent - 15 + 127) << 23) | (mantissa << 13);
		}
		return std::bit_cast<float>(bits);
	}

	// Bethesda vertex descriptor: bits 0-3 = vertex size in 4-byte units, bits 44+ = attribute
	// flags. The position is always first. (0.8.0 took NG's GetAttributeOffset(VA_POSITION),
	// which reads bits 4-7 - the dynamic vertex size - and produced pieces 20-60 thousand units big.)
	// 0.8.2 raw bytes: the static meshes' CPU copy holds 32-bit float positions (16 bytes with the
	// bitangent X) although VF_FULLPREC is not set. The space before the next attribute decides:
	// nibbles 2..9 are the offsets (in 4-byte units) of UV1, UV2, normal, tangent, color, ...
	struct VertexLayout
	{
		std::uint32_t stride{ 0 };
		bool          fullPrecision{ false };
	};

	VertexLayout LayoutOf(std::uint64_t a_desc)
	{
		VertexLayout  layout{ .stride = static_cast<std::uint32_t>(a_desc & 0xF) * 4 };
		std::uint32_t nextAttribute = layout.stride;
		for (int nibble = 2; nibble <= 9; ++nibble) {
			const auto offset = static_cast<std::uint32_t>((a_desc >> (4 * nibble)) & 0xF) * 4;
			if (offset > 0) {
				nextAttribute = (std::min)(nextAttribute, offset);
			}
		}
		layout.fullPrecision = ((a_desc >> 44) & RE::BSGraphics::Vertex::VF_FULLPREC) != 0 || nextAttribute >= 12;
		return layout;
	}

	RE::NiPoint3 PositionAt(const std::uint8_t* a_vertex, bool a_fullPrecision)
	{
		if (a_fullPrecision) {
			RE::NiPoint3 position;
			std::memcpy(&position, a_vertex, sizeof(float) * 3);
			return position;
		}
		std::uint16_t half[3];
		std::memcpy(half, a_vertex, sizeof(half));
		return { HalfToFloat(half[0]), HalfToFloat(half[1]), HalfToFloat(half[2]) };
	}

	std::uint32_t Find(std::vector<std::uint32_t>& a_parent, std::uint32_t a_v)
	{
		while (a_parent[a_v] != a_v) {
			a_parent[a_v] = a_parent[a_parent[a_v]];
			a_v = a_parent[a_v];
		}
		return a_v;
	}

	// Connected pieces of a shape (triangles sharing vertices): with window textures one piece is
	// about one window. Returns false when the shape has no CPU copy of its vertices.
	bool SplitShape(RE::FormID a_ref, RE::BSGeometry* a_geometry, const std::string& a_texture, std::vector<Window>& a_out)
	{
		auto* triShape = a_geometry->AsTriShape();
		auto& data = a_geometry->GetGeometryRuntimeData();
		auto* renderer = data.rendererData;
		if (!triShape || !renderer || !renderer->rawVertexData || !renderer->rawIndexData) {
			return false;
		}
		const auto&         counts = triShape->GetTrishapeRuntimeData();
		const std::uint32_t vertices = counts.vertexCount;
		const std::uint32_t triangles = counts.triangleCount;
		const auto          desc = std::bit_cast<std::uint64_t>(data.vertexDesc);
		const auto [stride, fullPrecision] = LayoutOf(desc);
		if (vertices == 0 || triangles == 0 || vertices > kMaxSplitVertices || stride < (fullPrecision ? 12u : 6u)) {
			return false;
		}

		std::vector<std::uint8_t>  vertexData(static_cast<std::size_t>(vertices) * stride);
		std::vector<std::uint16_t> indices(static_cast<std::size_t>(triangles) * 3);
		if (!SafeCopy(vertexData.data(), renderer->rawVertexData, vertexData.size()) ||
			!SafeCopy(indices.data(), renderer->rawIndexData, indices.size() * sizeof(std::uint16_t))) {
			return false;
		}

		// 0.8.1: every piece came out with size 0, as if all positions were equal. The first shapes'
		// raw bytes go to the trace to see what the CPU copy holds.
		if (g_rawSamples.size() < 4) {
			std::string bytes;
			const auto  shown = (std::min)(vertexData.size(), std::size_t{ 96 });
			for (std::size_t i = 0; i < shown; ++i) {
				bytes += std::format("{:02X}{}", vertexData[i], (i + 1) % stride == 0 ? "|" : "");
			}
			g_rawSamples.push_back(Trace::Args{}
									   .Str("shape", a_geometry->name.c_str())
									   .Str("texture", a_texture)
									   .Hex("desc", desc)
									   .Int("stride", stride)
									   .Bool("fullPrecision", fullPrecision)
									   .Int("vertices", vertices)
									   .Int("triangles", triangles)
									   .Hex("rawVertexData", reinterpret_cast<std::uintptr_t>(renderer->rawVertexData))
									   .Str("bytes", bytes)
									   .Json());
		}

		std::vector<RE::NiPoint3> positions(vertices);
		for (std::uint32_t v = 0; v < vertices; ++v) {
			positions[v] = PositionAt(vertexData.data() + static_cast<std::size_t>(v) * stride, fullPrecision);
		}

		std::vector<std::uint32_t> parent(vertices);
		std::iota(parent.begin(), parent.end(), 0u);
		for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
			const std::uint32_t a = indices[t], b = indices[t + 1], c = indices[t + 2];
			if (a >= vertices || b >= vertices || c >= vertices) {
				continue;
			}
			parent[Find(parent, b)] = Find(parent, a);
			parent[Find(parent, c)] = Find(parent, a);
		}

		struct Piece
		{
			RE::NiPoint3  sum;
			RE::NiPoint3  min{ FLT_MAX, FLT_MAX, FLT_MAX };
			RE::NiPoint3  max{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
			RE::NiPoint3  normal;
			RE::NiPoint3  firstNormal;
			std::uint32_t vertices{ 0 };
			std::uint32_t triangles{ 0 };
		};
		std::unordered_map<std::uint32_t, Piece> pieces;
		for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
			const std::uint32_t a = indices[t], b = indices[t + 1], c = indices[t + 2];
			if (a >= vertices || b >= vertices || c >= vertices) {
				continue;
			}
			auto&      piece = pieces[Find(parent, a)];
			const auto n = (positions[b] - positions[a]).Cross(positions[c] - positions[a]);
			if (piece.triangles++ == 0) {
				piece.firstNormal = n;
			}
			// Both faces of a pane would cancel out: align every triangle with the first one.
			piece.normal += n.Dot(piece.firstNormal) < 0.0f ? -n : n;
		}
		for (std::uint32_t v = 0; v < vertices; ++v) {
			auto it = pieces.find(Find(parent, v));
			if (it == pieces.end()) {
				continue;
			}
			auto&       piece = it->second;
			const auto& p = positions[v];
			piece.sum += p;
			piece.min = { (std::min)(piece.min.x, p.x), (std::min)(piece.min.y, p.y), (std::min)(piece.min.z, p.z) };
			piece.max = { (std::max)(piece.max.x, p.x), (std::max)(piece.max.y, p.y), (std::max)(piece.max.z, p.z) };
			++piece.vertices;
		}

		std::vector<const Piece*> ordered;
		for (const auto& [root, piece] : pieces) {
			if (piece.vertices > 0) {
				ordered.push_back(&piece);
			}
		}
		std::ranges::sort(ordered, std::greater{}, &Piece::triangles);
		if (ordered.size() > kMaxPiecesPerShape) {
			ordered.resize(kMaxPiecesPerShape);
		}

		const auto& world = a_geometry->world;
		std::size_t added = 0;
		for (const auto* piece : ordered) {
			auto normal = world.rotate * piece->normal;
			if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z) || normal.Unitize() == 0.0f) {
				normal = {};
			}
			const auto extent = piece->max - piece->min;
			const auto center = world * (piece->sum / static_cast<float>(piece->vertices));
			const auto size = (std::max)({ extent.x, extent.y, extent.z }) * world.scale;
			if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) || !std::isfinite(size) || size > kMaxWindowSize) {
				++g_windowStats.piecesRejected;
				continue;
			}
			a_out.push_back({ .ref = a_ref,
				.texture = a_texture,
				.center = center,
				.normal = normal,
				.size = size,
				.triangles = piece->triangles,
				.piece = true });
			++added;
		}
		g_windowStats.pieces += added;
		return true;
	}

	std::vector<Window> ScanReference(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root)
	{
		std::vector<Window> windows;
		const auto          id = a_ref->GetFormID();
		RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
			const auto texture = DiffuseTexture(a_geometry);
			if (!IsWindowPath(texture)) {
				return RE::BSVisit::BSVisitControl::kContinue;
			}
			++g_windowStats.shapes;
			const auto name = FileName(texture);
			if (SplitShape(id, a_geometry, name, windows)) {
				++g_windowStats.shapesSplit;
			} else {
				++g_windowStats.shapesNoVertices;
				windows.push_back({ .ref = id,
					.texture = name,
					.center = a_geometry->worldBound.center,
					.size = a_geometry->worldBound.radius * 2.0f });
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});

		// Kit pieces named "...window..." whose window part has a generic texture.
		if (windows.empty()) {
			auto*       base = a_ref->GetBaseObject();
			const auto* model = base ? skyrim_cast<RE::TESModel*>(base) : nullptr;
			const char* path = model ? model->GetModel() : nullptr;
			if (path && IsWindowPath(path)) {
				windows.push_back({ .ref = id, .texture = FileName(path), .center = a_root->worldBound.center, .size = a_root->worldBound.radius * 2.0f });
			}
		}
		return windows;
	}

	struct NearWindow
	{
		const Window* window;
		std::size_t   index;  // within its reference
		float         distance;
	};

	std::vector<NearWindow> NearestWindows(const RE::NiPoint3& a_playerPos, std::size_t a_count)
	{
		std::vector<NearWindow> all;
		for (const auto& [ref, windows] : g_windowCache) {
			for (std::size_t i = 0; i < windows.size(); ++i) {
				all.push_back({ &windows[i], i, windows[i].center.GetDistance(a_playerPos) });
			}
		}
		const auto count = (std::min)(a_count, all.size());
		std::ranges::partial_sort(all, all.begin() + static_cast<std::ptrdiff_t>(count), {}, &NearWindow::distance);
		all.resize(count);
		return all;
	}

	std::string WindowJson(const NearWindow& a_near)
	{
		const auto& w = *a_near.window;
		Trace::Args args;
		args.Form("ref", RE::TESForm::LookupByID(w.ref))
			.Int("index", static_cast<std::int64_t>(a_near.index))
			.Str("texture", w.texture)
			.Pos("center", w.center)
			.Num("dist", a_near.distance)
			.Num("size", w.size)
			.Int("triangles", w.triangles)
			.Bool("piece", w.piece);
		if (w.normal.x != 0.0f || w.normal.y != 0.0f || w.normal.z != 0.0f) {
			args.Pos("normal", w.normal).Num("facingYawDeg", YawDegrees(w.normal));
		}
		return args.Json();
	}

	std::string WindowText(const NearWindow& a_near)
	{
		const auto& w = *a_near.window;
		auto text = std::format("{:.1f} m", a_near.distance / kUnitsPerMeter);
		if (w.normal.x != 0.0f || w.normal.y != 0.0f) {
			text += std::format(", facing {:.0f} deg", YawDegrees(w.normal));
		}
		return text + ", " + w.texture;
	}

	std::size_t WindowCount()
	{
		std::size_t count = 0;
		for (const auto& [ref, windows] : g_windowCache) {
			count += windows.size();
		}
		return count;
	}

	void TraceWindows(std::string_view a_reason, const std::vector<NearWindow>& a_nearest)
	{
		std::string windows;
		for (std::size_t i = 0; i < a_nearest.size(); ++i) {
			windows += (i ? "," : "") + WindowJson(a_nearest[i]);
		}
		Trace::Write("detector.windows", Trace::Args{}
											 .Str("reason", a_reason)
											 .Int("windows", static_cast<std::int64_t>(WindowCount()))
											 .Int("refsCached", static_cast<std::int64_t>(g_windowCache.size()))
											 .Int("refsScanned", static_cast<std::int64_t>(g_windowStats.refsScanned))
											 .Int("shapes", static_cast<std::int64_t>(g_windowStats.shapes))
											 .Int("shapesSplit", static_cast<std::int64_t>(g_windowStats.shapesSplit))
											 .Int("shapesNoVertices", static_cast<std::int64_t>(g_windowStats.shapesNoVertices))
											 .Int("pieces", static_cast<std::int64_t>(g_windowStats.pieces))
											 .Int("piecesRejected", static_cast<std::int64_t>(g_windowStats.piecesRejected))
											 .Raw("rawSamples", "[" + std::accumulate(g_rawSamples.begin(), g_rawSamples.end(), std::string{}, [](std::string a_all, const std::string& a_one) {
												 return a_all.empty() ? a_one : a_all + "," + a_one;
											 }) + "]")
											 .Raw("nearest", "[" + windows + "]"));
	}

	// ------------------------------------------------------------------
	// What the camera looks at (F6): to find the textures of windows the keywords miss
	// ------------------------------------------------------------------
	struct ViewHit
	{
		RE::FormID  ref{ 0 };
		std::string model;
		std::string shape;
		std::string texture;
		float       enter{ 0.0f };  // distance along the view ray to the bounding sphere
		float       radius{ 0.0f };
		bool        window{ false };
	};

	bool RayEntersSphere(const RE::NiPoint3& a_origin, const RE::NiPoint3& a_direction, const RE::NiBound& a_bound, float& a_enter)
	{
		const auto  toCenter = a_bound.center - a_origin;
		const float along = toCenter.Dot(a_direction);
		const float missSquared = toCenter.Dot(toCenter) - along * along;
		const float radiusSquared = a_bound.radius * a_bound.radius;
		if (a_bound.radius <= 0.0f || missSquared > radiusSquared) {
			return false;
		}
		a_enter = along - std::sqrt(radiusSquared - missSquared);
		return along + a_bound.radius >= 0.0f;  // not entirely behind the camera
	}

	RE::NiPoint3 ToPoint(const RE::hkVector4& a_vector)
	{
		alignas(16) float values[4];
		_mm_store_ps(values, a_vector.quad);
		return { values[0], values[1], values[2] };
	}

	// Havok ray cast along the view (line-of-sight layer): the reference really hit, and where.
	// 0.8.1 sorted bounding spheres only, and the large spheres of buildings never came first.
	// References whose 3D is hidden (InteriorSim hides the tavern door, its collision stays) are
	// looked through: the ray goes on behind them; a_skipped lists them.
	bool PickView(const RE::NiPoint3& a_origin, const RE::NiPoint3& a_direction, RE::NiPointer<RE::TESObjectREFR>& a_ref, RE::NiPoint3& a_point,
		RE::NiPoint3& a_normal, std::string& a_skipped)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		auto* world = cell ? cell->GetbhkWorld() : nullptr;
		if (!world) {
			return false;
		}
		const float scale = RE::bhkWorld::GetWorldScale();
		const auto  end = a_origin + a_direction * kLookRadius;
		auto        from = a_origin;
		for (int attempt = 0; attempt < kMaxLookThrough; ++attempt) {
			RE::bhkPickData pick;
			pick.rayInput.from = RE::hkVector4(from.x * scale, from.y * scale, from.z * scale, 0.0f);
			pick.rayInput.to = RE::hkVector4(end.x * scale, end.y * scale, end.z * scale, 0.0f);
			pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
			world->PickObject(pick);
			if (!pick.rayOutput.HasHit()) {
				return false;
			}
			a_point = from + (end - from) * pick.rayOutput.hitFraction;
			a_normal = ToPoint(pick.rayOutput.normal);
			auto*       ref = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
			const auto* root = ref ? ref->Get3D() : nullptr;
			if (root && root->GetAppCulled()) {
				a_skipped += std::format("{}{:08X}", a_skipped.empty() ? "" : ",", ref->GetFormID());
				from = a_point + a_direction * kLookThroughStep;
				continue;
			}
			a_ref.reset(ref);
			return true;
		}
		return false;
	}

	// a_hitDistance >= 0 when the ray hit a reference: then the hits are that reference's shapes.
	std::vector<ViewHit> LookAt(Trace::Args& a_how, float& a_hitDistance, std::string& a_skipped)
	{
		std::vector<ViewHit> hits;
		a_hitDistance = -1.0f;
		auto*                camera = RE::Main::WorldRootCamera();
		auto*                player = RE::PlayerCharacter::GetSingleton();
		if (!camera || !player) {
			return hits;
		}
		const auto origin = camera->world.translate;
		// Gamebryo cameras look along their local +X; checked against the player's aim (pitch > 0 = down).
		const RE::NiPoint3 cameraX{ camera->world.rotate.entry[0][0], camera->world.rotate.entry[1][0], camera->world.rotate.entry[2][0] };
		const float        yaw = player->GetAngleZ();
		const float        pitch = player->GetAngleX();
		const RE::NiPoint3 aim{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
		const float        agreement = cameraX.Dot(aim);
		const auto         direction = agreement > 0.5f ? cameraX : aim;
		a_how.Str("direction", agreement > 0.5f ? "camera +X" : "player aim").Num("cameraDotAim", agreement).Pos("origin", origin).Pos("ray", direction);

		// The hit reference first: its shapes ordered by how close their bounds are to the hit point.
		RE::NiPointer<RE::TESObjectREFR> hitRef;
		RE::NiPoint3                     hitPoint, hitNormal;
		const bool                       picked = PickView(origin, direction, hitRef, hitPoint, hitNormal, a_skipped);
		a_how.Str("lookedThroughHidden", a_skipped);
		if (picked) {
			a_how.Bool("picked", true).Pos("hitPoint", hitPoint).Pos("hitNormal", hitNormal).Num("hitDistance", hitPoint.GetDistance(origin)).Form("hitRef", hitRef.get());
			if (auto* root = hitRef ? hitRef->Get3D() : nullptr) {
				a_hitDistance = hitPoint.GetDistance(origin);
				auto*       base = hitRef->GetBaseObject();
				const auto* model = base ? skyrim_cast<RE::TESModel*>(base) : nullptr;
				const char* modelPath = model ? model->GetModel() : nullptr;
				RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geometry) {
					const auto& bound = a_geometry->worldBound;
					const auto  texture = DiffuseTexture(a_geometry);
					hits.push_back({ .ref = hitRef->GetFormID(),
						.model = FileName(modelPath ? modelPath : ""),
						.shape = a_geometry->name.c_str(),
						.texture = FileName(texture),
						.enter = bound.center.GetDistance(hitPoint) - bound.radius,  // < 0: the hit point is inside the bound
						.radius = bound.radius,
						.window = IsWindowPath(texture) });
					return RE::BSVisit::BSVisitControl::kContinue;
				});
				std::ranges::sort(hits, [](const ViewHit& a_lhs, const ViewHit& a_rhs) {
					// inside the bound and small first: the window shape is smaller than the wall around it
					const bool lhsIn = a_lhs.enter <= 0.0f, rhsIn = a_rhs.enter <= 0.0f;
					if (lhsIn != rhsIn) {
						return lhsIn;
					}
					return lhsIn ? a_lhs.radius < a_rhs.radius : a_lhs.enter < a_rhs.enter;
				});
				if (hits.size() > kMaxLookHits) {
					hits.resize(kMaxLookHits);
				}
				return hits;
			}
		} else {
			a_how.Bool("picked", false);
		}

		std::vector<RE::NiPointer<RE::TESObjectREFR>> refs;
		for (auto* cell : LoadedCells()) {
			cell->ForEachReferenceInRange(origin, kLookRadius, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && !a_ref->IsPlayerRef() && !a_ref->IsDisabled() && a_ref->Get3D()) {
					refs.emplace_back(a_ref);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
		for (const auto& ref : refs) {
			auto* root = ref->Get3D();
			float enter = 0.0f;
			if (!root || !RayEntersSphere(origin, direction, root->worldBound, enter)) {
				continue;
			}
			auto*       base = ref->GetBaseObject();
			const auto* model = base ? skyrim_cast<RE::TESModel*>(base) : nullptr;
			const char* modelPath = model ? model->GetModel() : nullptr;
			RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geometry) {
				float shapeEnter = 0.0f;
				if (RayEntersSphere(origin, direction, a_geometry->worldBound, shapeEnter)) {
					const auto texture = DiffuseTexture(a_geometry);
					hits.push_back({ .ref = ref->GetFormID(),
						.model = FileName(modelPath ? modelPath : ""),
						.shape = a_geometry->name.c_str(),
						.texture = FileName(texture),
						.enter = (std::max)(shapeEnter, 0.0f),
						.radius = a_geometry->worldBound.radius,
						.window = IsWindowPath(texture) });
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}
		// Bounding spheres overlap a lot: nearest entry first, then the smaller shape.
		std::ranges::sort(hits, [](const ViewHit& a_lhs, const ViewHit& a_rhs) {
			return a_lhs.enter != a_rhs.enter ? a_lhs.enter < a_rhs.enter : a_lhs.radius < a_rhs.radius;
		});
		if (hits.size() > kMaxLookHits) {
			hits.resize(kMaxLookHits);
		}
		return hits;
	}

	void ReportLookAt()
	{
		Trace::Args args;
		float       hitDistance = -1.0f;
		std::string skipped;
		const auto  hits = LookAt(args, hitDistance, skipped);
		const bool  picked = hitDistance >= 0.0f && !hits.empty();
		const auto  through = skipped.empty() ? std::string{} : std::format(" through hidden {}", skipped);
		std::string json;
		std::string text = picked ? std::format("CLR: looking{} at {} ({:.0f} m), near the hit:", through, hits.front().model, hitDistance / kUnitsPerMeter) :
		                            std::format("CLR: looking{} at (bounds only):", through);
		std::size_t shown = 0;
		std::vector<std::string> seen;
		for (const auto& hit : hits) {
			json += (json.empty() ? "" : ",") + Trace::Args{}
													.Form("ref", RE::TESForm::LookupByID(hit.ref))
													.Str("model", hit.model)
													.Str("shape", hit.shape)
													.Str("texture", hit.texture)
													.Num("enter", hit.enter)
													.Num("radius", hit.radius)
													.Bool("window", hit.window)
													.Json();
			if (shown < 4 && std::ranges::find(seen, hit.texture) == seen.end()) {
				seen.push_back(hit.texture);
				const auto name = hit.texture.empty() ? hit.shape : hit.texture;
				text += picked ? std::format("{} {}{}", shown ? "," : "", name, hit.window ? " (window)" : "") :
				                 std::format("{} {} {:.0f} m{}", shown ? "," : "", name, hit.enter / kUnitsPerMeter, hit.window ? " (window)" : "");
				++shown;
			}
		}
		Trace::Write("detector.look", args.Raw("hits", "[" + json + "]"), nullptr, true);
		Hud::Show("detectors.look", hits.empty() ? "CLR: looking at: nothing within 40 m" : text, true);
	}

	void UpdateWindows(const RE::NiPoint3& a_playerPos)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* playerCell = player ? player->GetParentCell() : nullptr;
		if (!playerCell) {
			return;
		}
		const void* space = playerCell->IsInteriorCell() ? static_cast<const void*>(playerCell) : playerCell->GetRuntimeData().worldSpace;
		if (space != g_windowSpace) {
			g_windowCache.clear();
			g_windowSpace = space;
		}

		// Collect first, scan after: the cell's reference lock is held inside ForEachReference.
		std::vector<RE::NiPointer<RE::TESObjectREFR>> refs;
		for (auto* cell : LoadedCells()) {
			cell->ForEachReferenceInRange(a_playerPos, Settings::Get().windowRadius, [&](RE::TESObjectREFR* a_ref) {
				if (refs.size() < kMaxNewRefsPerScan && a_ref && !a_ref->IsDisabled() && !a_ref->As<RE::Actor>() &&
					!g_windowCache.contains(a_ref->GetFormID()) && a_ref->Get3D()) {
					refs.emplace_back(a_ref);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
		for (const auto& ref : refs) {
			if (auto* root = ref->Get3D()) {
				g_windowCache.emplace(ref->GetFormID(), ScanReference(ref.get(), root));
				++g_windowStats.refsScanned;
			}
		}

		const auto nearest = NearestWindows(a_playerPos, Settings::Get().nearestCount);
		std::vector<std::pair<RE::FormID, std::size_t>> keys;
		for (const auto& n : nearest) {
			keys.emplace_back(n.window->ref, n.index);
		}
		if (keys != g_tracedWindows) {
			g_tracedWindows = keys;
			TraceWindows("changed", NearestWindows(a_playerPos, kMaxTracedWindows));
		}
		const auto nearestKey = keys.empty() ? std::pair<RE::FormID, std::size_t>{ 0, 0 } : keys.front();
		if (nearestKey != g_nearestWindow) {
			g_nearestWindow = nearestKey;
			if (!nearest.empty() && Settings::Get().detectorsNotify) {
				Hud::Show("window", "CLR: window " + WindowText(nearest.front()));
			}
		}
	}
}

namespace Detectors
{
	void Tick()
	{
		const auto& settings = Settings::Get();
		auto*       player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->GetParentCell()) {
			return;
		}
		const auto now = Trace::NowUs();
		const auto pos = player->GetPosition();
		if (now >= g_nextDoorsUs) {
			g_nextDoorsUs = now + static_cast<std::int64_t>(settings.doorsIntervalSeconds * 1'000'000.0f);
			if (settings.detectorsEnabled) {
				UpdateDoors(pos);
			} else {
				ScanDoors(pos);  // silent: NearestDoorTo serves [Preload] and [InteriorSim]
			}
		}
		if (settings.detectorsEnabled && settings.windowsEnabled && now >= g_nextWindowsUs) {
			g_nextWindowsUs = now + static_cast<std::int64_t>(settings.windowsIntervalSeconds * 1'000'000.0f);
			UpdateWindows(pos);
		}
	}

	void Report()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->GetParentCell()) {
			return;
		}
		if (!Settings::Get().detectorsEnabled) {
			ReportLookAt();
			return;
		}
		const auto pos = player->GetPosition();
		UpdateDoors(pos);
		if (Settings::Get().windowsEnabled) {
			UpdateWindows(pos);
		}

		// Shown in this order: urgent messages go to the head of the queue, so the last one comes first.
		const auto nearest = NearestWindows(pos, 1);
		auto       windows = std::format("CLR: windows {} in {} shapes ({} split, {} bound only)", WindowCount(), g_windowStats.shapes,
				  g_windowStats.shapesSplit, g_windowStats.shapesNoVertices);
		if (!nearest.empty()) {
			windows += " | nearest " + WindowText(nearest.front());
		}
		Hud::Show("detectors.windows", std::move(windows), true);

		auto doors = std::format("CLR: doors {}", g_doors.size());
		for (std::size_t i = 0; i < g_doors.size() && i < 3; ++i) {
			doors += " | " + DoorText(g_doors[i]);
		}
		Hud::Show("detectors.doors", std::move(doors), true);

		TraceDoors("hotkey");
		TraceWindows("hotkey", NearestWindows(pos, kMaxTracedWindows));
		ReportLookAt();
	}

	std::optional<Door> NearestDoorTo(const RE::TESObjectCELL* a_cell)
	{
		if (!a_cell) {
			return std::nullopt;
		}
		const auto id = a_cell->GetFormID();
		for (const auto& door : g_doors) {
			if (door.destCell == id) {
				return door;
			}
		}
		return std::nullopt;
	}

	std::vector<RE::TESObjectCELL*> SceneCells()
	{
		return LoadedCells();
	}

	std::string TextureFile(RE::BSGeometry* a_geometry)
	{
		return FileName(DiffuseTexture(a_geometry));
	}

	bool WorldPoints(RE::BSGeometry* a_geometry, std::vector<RE::NiPoint3>& a_out)
	{
		auto* triShape = a_geometry->AsTriShape();
		auto& data = a_geometry->GetGeometryRuntimeData();
		auto* renderer = data.rendererData;
		if (!triShape || !renderer || !renderer->rawVertexData) {
			return false;
		}
		const std::uint32_t vertices = triShape->GetTrishapeRuntimeData().vertexCount;
		const auto [stride, fullPrecision] = LayoutOf(std::bit_cast<std::uint64_t>(data.vertexDesc));
		if (vertices == 0 || vertices > kMaxSplitVertices || stride < (fullPrecision ? 12u : 6u)) {
			return false;
		}
		std::vector<std::uint8_t> vertexData(static_cast<std::size_t>(vertices) * stride);
		if (!SafeCopy(vertexData.data(), renderer->rawVertexData, vertexData.size())) {
			return false;
		}
		const auto& world = a_geometry->world;
		for (std::uint32_t v = 0; v < vertices; ++v) {
			const auto local = PositionAt(vertexData.data() + static_cast<std::size_t>(v) * stride, fullPrecision);
			a_out.push_back(world * local);
		}
		return true;
	}

	RE::NiPointer<RE::TESObjectREFR> PickRef(const RE::NiPoint3& a_origin, const RE::NiPoint3& a_direction, RE::NiPoint3& a_point)
	{
		RE::NiPointer<RE::TESObjectREFR> ref;
		RE::NiPoint3                     normal;
		std::string                      skipped;
		if (!PickView(a_origin, a_direction, ref, a_point, normal, skipped)) {
			ref.reset();
		}
		return ref;
	}
}
