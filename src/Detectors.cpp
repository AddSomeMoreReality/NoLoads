#include "Detectors.h"

#include "Hud.h"
#include "Settings.h"
#include "Trace.h"

namespace
{
	constexpr std::uint32_t kAttached = 7;            // TESObjectCELL::CellState
	constexpr float         kUnitsPerMeter = 70.0f;   // one game unit is about 1.43 cm
	constexpr std::uint32_t kMaxSplitVertices = 20'000;  // larger shapes are not read
	constexpr float         kLookRadius = 3000.0f;     // F6: references around the camera checked against the view ray
	constexpr std::size_t   kMaxLookHits = 12;
	constexpr int           kMaxLookThrough = 4;       // F6: hidden references the ray may pass through
	constexpr float         kLookThroughStep = 5.0f;   // units the ray restarts behind a hidden reference
	constexpr std::size_t   kMaxTracedDoors = 40;

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
	}

	// ------------------------------------------------------------------
	// Shapes: diffuse textures and vertices
	// ------------------------------------------------------------------
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

	// ------------------------------------------------------------------
	// What the camera looks at (F6)
	// ------------------------------------------------------------------
	struct ViewHit
	{
		RE::FormID  ref{ 0 };
		std::string model;
		std::string shape;
		std::string texture;
		float       enter{ 0.0f };  // distance along the view ray to the bounding sphere
		float       radius{ 0.0f };
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
						.radius = bound.radius });
					return RE::BSVisit::BSVisitControl::kContinue;
				});
				std::ranges::sort(hits, [](const ViewHit& a_lhs, const ViewHit& a_rhs) {
					// inside the bound and small first: a window or a sign is smaller than the wall around it
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
						.radius = a_geometry->worldBound.radius });
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
													.Json();
			if (shown < 4 && std::ranges::find(seen, hit.texture) == seen.end()) {
				seen.push_back(hit.texture);
				const auto name = hit.texture.empty() ? hit.shape : hit.texture;
				text += picked ? std::format("{} {}", shown ? "," : "", name) :
				                 std::format("{} {} {:.0f} m", shown ? "," : "", name, hit.enter / kUnitsPerMeter);
				++shown;
			}
		}
		Trace::Write("detector.look", args.Raw("hits", "[" + json + "]"), nullptr, true);
		Hud::Show("detectors.look", hits.empty() ? "CLR: looking at: nothing within 40 m" : text);
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
		auto doors = std::format("CLR: doors {}", g_doors.size());
		for (std::size_t i = 0; i < g_doors.size() && i < 3; ++i) {
			doors += " | " + DoorText(g_doors[i]);
		}
		Hud::Show("detectors.doors", std::move(doors));

		TraceDoors("hotkey");
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
}
