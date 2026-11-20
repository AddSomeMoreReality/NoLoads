# Стадии InteriorSim 1–5: что это было и как вернуть

С 0.8.12 клавиша F8 переключает только «выключено» ↔ «портал» (бывшая стадия 6). Стадии 1–5 убраны из переключения по просьбе пользователя от 25.10.2026.

- **Стадии 1 и 2 остались в коде:** шаг физики интерьера и подъём его NPC до уровня high теперь входят в режим портала. Без них в портале не было бы живого интерьера.
- **Код стадий 3–5 удалён** из `src/InteriorSim.cpp`. Это отрисовка `cell3D` интерьера в сцене улицы, подъём над зданием, копия интерьера, контрольные копии двери и скрытие куска здания в проёме.
- **Полные исходники 0.8.11** (последняя версия со всеми стадиями) лежат в [archive/interior-sim-0.8.11/](archive/interior-sim-0.8.11/): `InteriorSim.cpp`, `InteriorSim.h`, `Settings.h` и секция `[InteriorSim]` из INI.
- Подробные разборы прогонов — в [FINDINGS.md](FINDINGS.md), раздел «H14–H16».

## Сводка

| Стадия F8 (до 0.8.12) | Что делала | Итог |
|---|---|---|
| 0 | выключено | — |
| 1 | шаг мира Havok интерьера в потоке физики сразу после мира игрока | работает с 0.8.1: ~58 шагов в секунду, кадр не дольше. **Оставлено** в режиме портала |
| 2 | плюс `Actor::MoveToHigh()` раз в секунду для NPC интерьера | работает: 6/6–7/7 NPC на high, ИИ ходит, бард поёт за дверью. **Оставлено** в режиме портала |
| 3 | плюс `cell3D` интерьера в сцене улицы: дверь интерьера, развёрнутая на 180°, на двери снаружи | рисуется с 0.8.5, но интерьер больше здания и торчит из стен, маркеры видны, NPC в A-позе |
| 4 | то же, поднятое на `fLiftHeight` над зданием | рисуется над крышей (0.8.5) |
| 5 | над дверью копия интерьера: клоны 3D ссылок без актёров и двери, узел в `ShadowSceneNode::AttachObject` | рисуется над крышей, темнее стадии 4 (0.8.5). Клоны не освобождаются: в 0.8.2 деструктор клона скелета NPC уронил игру |
| 6 | портал: вторая камера рисует интерьер в текстуру на месте двери | с 0.8.12 — единственный режим F8 (номер 1) |

На стадиях 3–5 также:
- на улице стояли 2 контрольные копии двери (`HangControls`);
- скрывались наружная дверь, дверь интерьера в проёме и кусок здания с текстурой двери (`HideDoorway`, `[InteriorSim] bHideExteriorDoor`);
- счётчики `OnVisible` следили за зданием, узлом ячейки, нашим узлом, интерьером и копиями двери. Сами счётчики остались для портала.

## Что выяснилось и почему так устроено

- **Рисуется только то, что висит в узлах ячеек экстерьера.**
  - Отсечение игры обходит корень каждой ячейки (`BSMultiBoundNode` под `ObjectLODRoot`).
  - Под `ObjectLODRoot`, `ShadowSceneNode` и `WorldRoot` оно до добавленных узлов не доходило (0.8.4).
  - Поэтому наш узел добавлялся в конец детей корня ячейки, где стоит наружная дверь. `AttachChild(child, false)`: первые пустые места у корня ячейки отведены под типы ссылок.
- **У `cell3D` выгруженного из сцены интерьера устаревшие границы** (радиус 1 в начале координат). После подвешивания нужен `Update`.
  - Обновлять нужно с `NiUpdateData::kDisableCollision`, чтобы не двигать тела Havok интерьера.
- **Перед подвешиванием снимались `AppCulled` и поднимался `BSFadeNode::currentFade`** со всего поддерева (`Walk(reveal)`). Из-за этого видны маркеры и скрытые игрой варианты геометрии.
- **Узел надо снимать раньше, чем игра отсоединит ячейку:**
  - при `Loading Menu` open;
  - в `GridCellArray::DetachAll`/`Detach`/`ClearItem`/`KillAll`;
  - при любой смене состояния интерьера;
  - если дверь ушла из загруженных ячеек.
- **«Другая дверь» в проёме таверны** — кусок здания `WRHouseBanneredMare01:16` (`wrdoor01.dds`) в постоянной ячейке мира. Его находили лучами Havok в проём и поиском ссылок в постоянной ячейке.
- **Клоны (стадия 5 и копии двери) нельзя освобождать.** Они живут до конца сессии (`g_graveyard`). Актёры не клонируются.

## Как вернуть стадии 3–5

Проще всего вернуть файлы 0.8.11 целиком:
1. Скопировать `docs/archive/interior-sim-0.8.11/InteriorSim.cpp` и `InteriorSim.h` в `src/`.
2. В `Settings.h`/`Settings.cpp` вернуть поля `interiorSimLift` (`[InteriorSim] fLiftHeight`, 1500) и `interiorSimHideDoor` (`bHideExteriorDoor`, 1), а `interiorSimStage` снова ограничить 0..6.
3. В INI вернуть ключи из `docs/archive/interior-sim-0.8.11/InteriorSim.ini`.

Остальное (`Portal`, хуки `GridCellArray`, `OnVisible`, `bhkWorld::vfunc[0x32]`, `GuardWorlds`) не менялось. Портал в версии 0.8.11 вызывается из `AttachPortal`, тоже в архивном файле.

Ниже — ключевой код каждой стадии из архивной версии (пространство имён без имени в `InteriorSim.cpp`).

### Стадия 1: шаг физики интерьера

Detour на `bhkWorld::vfunc[0x32]` (`AE#77851`) в `src/Hooks.cpp`. Он есть и в 0.8.12:

```cpp
// bhkWorld::vfunc[0x32] (also bhkWorldM's): one step of a Bethesda physics world; the time step
// comes from globals (AE#77921, the warm start loop, calls it with 1). After the game stepped the
// player's world, InteriorSim may step the kept interior's world the same way.
struct BhkWorldStep
{
	static std::uint64_t thunk(RE::bhkWorld* a_world, std::int32_t a_flag)
	{
		const auto result = func(a_world, a_flag);
		InteriorSim::AfterWorldStep(a_world, a_flag, func);
		return result;
	}

	static inline decltype(&thunk) func;
};
```

Сам шаг (в 0.8.12 тот же код, условие — режим портала):

```cpp
void AfterWorldStep(RE::bhkWorld* a_world, std::int32_t a_flag, StepFn a_step)
{
	if (g_stage.load() < 1 || g_transition.load() || Physics::InWarmStart() || a_world != g_playerWorld.load()) {
		return;
	}
	std::unique_lock lock{ g_worldLock, std::try_to_lock };
	if (!lock.owns_lock()) {
		++g_stepsBusy;  // the main thread is attaching, buffering or unloading interiors
		return;
	}
	auto* cell = Preload::Interior();
	if (!cell || Trace::CellState(cell) != kUnattached) {
		return;
	}
	auto* world = cell->GetbhkWorld();
	if (!world || world == a_world) {
		return;
	}
	auto&      active = *reinterpret_cast<std::uint8_t*>(reinterpret_cast<std::uintptr_t>(world) + kWorldActiveOffset);
	const auto wasActive = active;
	g_worldActive = wasActive;
	if (!wasActive) {
		if (!Settings::Get().interiorSimForceWorldActive) {
			return;
		}
		active = 1;
	}
	a_step(world, a_flag);
	active = wasActive;
	++g_steps;
	g_stepThread = REX::W32::GetCurrentThreadId();
}
```

Главный поток держит `GuardWorlds()` вокруг `TES::SetCurrentCell`, `PurgeBufferedCells`, `GridCellArray::DetachAll`, `AE#13370` и `AE#13434`. Между открытием и закрытием `Loading Menu` шагов нет (`SetTransition`).

### Стадия 2: NPC интерьера на уровне high

Раз в секунду из `Tick` (в 0.8.12 — в режиме портала):

```cpp
// ------------------------------------------------------------------
// AI
// ------------------------------------------------------------------
void PromoteActors(RE::TESObjectCELL* a_cell)
{
	// Collected first: MoveToHigh moves actors between the process lists, and the cell's
	// reference lock is held inside ForEachReference.
	std::vector<RE::NiPointer<RE::Actor>> actors;
	a_cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
		auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
		if (actor && !actor->IsPlayerRef() && !actor->IsDisabled() && !actor->IsDead()) {
			actors.emplace_back(actor);
		}
		return RE::BSContainer::ForEachResult::kContinue;
	});

	std::size_t promoted = 0;
	std::string levelsBefore;
	for (auto& actor : actors) {
		const auto level = actor->GetProcessLevel();
		levelsBefore += std::format("{}{}", levelsBefore.empty() ? "" : ",", static_cast<std::int32_t>(level));
		if (level != RE::PROCESS_TYPE::kHigh) {
			actor->MoveToHigh();
			++promoted;
		}
	}
	std::size_t high = 0;
	for (auto& actor : actors) {
		high += actor->GetProcessLevel() == RE::PROCESS_TYPE::kHigh ? 1 : 0;
	}
	g_actors = actors.size();
	g_highActors = high;
	g_promoted += promoted;
	if (promoted > 0) {
		Trace::Write("interior.sim", Trace::Args{}
										 .Str("action", "promote")
										 .Int("actors", static_cast<std::int64_t>(actors.size()))
										 .Int("promoted", static_cast<std::int64_t>(promoted))
										 .Int("highAfter", static_cast<std::int64_t>(high))
										 .Raw("levelsBefore", "[" + levelsBefore + "]"));
	}
}
```

### Стадии 3 и 4: `cell3D` интерьера в сцене улицы

Перенос координат: дверь интерьера, развёрнутая на 180° вокруг своей Z, ложится на наружную дверь (в 0.8.12 используется порталом):

```cpp
// Interior coordinates -> exterior: the interior door, turned around (180 deg about its own Z), lies
// on the exterior door: result * insideDoor = outsideDoor * flip. Only world transforms are used,
// so the game's Euler angle convention does not matter.
RE::NiTransform InteriorToExterior(const RE::NiTransform& a_outsideDoor, RE::NiTransform a_insideDoor)
{
	RE::NiTransform target = a_outsideDoor;
	RE::NiMatrix3   flip;
	flip.entry[0][0] = -1.0f;
	flip.entry[1][1] = -1.0f;
	target.rotate = target.rotate * flip;
	target.scale = 1.0f;
	a_insideDoor.scale = 1.0f;
	return target * a_insideDoor.Invert();
}

// The door's transform in the interior's own coordinates: its 3D world transform if that was
// computed, else the local one (cell nodes are at the origin), checked against the reference position.
std::optional<RE::NiTransform> InsideDoorTransform(RE::TESObjectREFR* a_door, std::string& a_source)
{
	const auto* node = a_door ? a_door->Get3D() : nullptr;
	if (!node) {
		return std::nullopt;
	}
	const auto pos = a_door->GetPosition();
	if (node->world.translate.GetDistance(pos) < 10.0f) {
		a_source = "world";
		return node->world;
	}
	if (node->local.translate.GetDistance(pos) < 10.0f) {
		a_source = "local";
		return node->local;
	}
	return std::nullopt;
}

// The root node of the exterior cell a_object hangs in: its ancestor right under ObjectLODRoot
// (TES::objRoot). Exterior cells' roots are BSMultiBoundNodes there.
RE::NiNode* CellRootOf(RE::NiAVObject* a_object)
{
	auto*       tes = RE::TES::GetSingleton();
	const auto* objectRoot = tes ? tes->objRoot : nullptr;
	for (auto* node = a_object ? a_object->parent : nullptr; node; node = node->parent) {
		if (node->parent == objectRoot && objectRoot) {
			return node;
		}
	}
	return nullptr;
}
```

Подвешивание (`a_mode` 3 — у двери, 4 — поднятый на `fLiftHeight`, 5 — копия, см. ниже):

```cpp
bool Attach(RE::TESObjectCELL* a_cell, std::uint32_t a_mode)
{
	const auto* loaded = a_cell->GetRuntimeData().loadedData;
	auto*       root = loaded ? loaded->cell3D.get() : nullptr;
	if (!root) {
		Fail("the interior has no 3D");
		return false;
	}
	if (root->parent) {
		Fail("the interior's 3D is already in a scene");
		return false;
	}
	const auto door = Detectors::NearestDoorTo(a_cell);
	if (!door) {
		Fail("no door to it in the loaded cells");
		return false;
	}
	const auto outside = door->ref.get();
	const auto inside = door->linked.get();
	auto*      outsideNode = outside ? outside->Get3D() : nullptr;
	if (!outsideNode) {
		Fail("the exterior door has no 3D");
		return false;
	}
	std::string source;
	const auto  insideTransform = InsideDoorTransform(inside.get(), source);
	if (!insideTransform) {
		Fail("the interior door has no usable 3D");
		return false;
	}
	// Hung where the game hangs the exterior cell's own references: in 0.8.4 only the copy of the door
	// in that cell's node was drawn; the culling never reached what was added under ObjectLODRoot.
	auto* parent = CellRootOf(outsideNode);
	if (!parent) {
		Fail("the exterior door is not in a cell's node under ObjectLODRoot");
		return false;
	}

	auto       transform = InteriorToExterior(outsideNode->world, *insideTransform);
	const auto lift = a_mode == kRenderAligned ? 0.0f : Settings::Get().interiorSimLift;
	transform.translate.z += lift;

	const bool  copyMode = a_mode == kRenderCopyLifted;
	std::size_t copied = 0;
	RE::NiAVObject* content = copyMode ? CopyInterior(a_cell, inside.get(), copied) : root;

	g_container.reset(RE::NiNode::Create(2));
	g_container->name = "CLR InteriorSim";
	g_container->local = parent->world.Invert() * transform;
	g_container->SetAppCulled(false);
	g_container->AttachChild(content, true);
	const auto parentChildren = ChildrenOf(parent);
	parent->AttachChild(g_container.get());  // at the end: the empty slots of a cell's root belong to reference types
	g_host.reset(parent);
	g_hostRoot = parent;

	TreeStats revealed;
	Walk(content, revealed, true, !copyMode);
	auto update = NoCollisionUpdate();
	g_container->Update(update);
	if (copyMode) {
		RegisterForLight(g_container.get());
	}
	g_cell3D.reset(root);
	g_renderMode = a_mode;
	g_renderFailure.clear();

	// User requests (24.10.2026): see inside through the doorway - hide the exterior door, the
	// interior's own door that now stands in the doorway (stages 3 and 4; the copy skips it), and
	// what the building has behind its door.
	RE::NiPoint3 ground, outward;
	Street(inside.get(), outsideNode, ground, outward);
	std::string rayHits;
	const auto  nearby = NearbyRefs(outsideNode->worldBound.center, outside.get(), outward, rayHits);
	std::string doorway = "[]";
	std::size_t doorwayHidden = 0;
	if (Settings::Get().interiorSimHideDoor) {
		HideShown(outsideNode);
		if (!copyMode && inside) {
			HideShown(inside->Get3D());
		}
		doorway = HideDoorway(nearby, outsideNode, outward, doorwayHidden);
	}

	// Culling counters: what the game draws itself (the building, the door's node, the cell's root),
	// then ours.
	ClearWatch();
	g_visibleSinceUs = Trace::NowUs();
	WatchTree("building", BuildingAround(outsideNode->worldBound.center, nearby));
	Watch("door's node", outsideNode->parent);
	Watch("cell root", parent);
	Watch("container", g_container.get());
	WatchInterior(content, outsideNode->world.translate + RE::NiPoint3{ 0.0f, 0.0f, lift });
	const auto controls = HangControls(outsideNode, ground, outward);

	RE::NiPoint3 insideDoorNow{};
	if (!copyMode && inside) {
		if (const auto* node = inside->Get3D()) {
			insideDoorNow = node->world.translate;
		}
	}

	Trace::Write("interior.sim", Trace::Args{}
									 .Str("action", "attach")
									 .Int("mode", a_mode)
									 .Bool("copy", copyMode)
									 .Int("copiedReferences", static_cast<std::int64_t>(copied))
									 .Form("door", outside.get())
									 .Form("insideDoor", inside.get())
									 .Str("insideDoorSource", source)
									 .Pos("outsideDoorWorld", outsideNode->world.translate)
									 .Pos("insideDoorWorldNow", insideDoorNow)
									 .Num("lift", lift)
									 .Str("parent", Ancestry(parent))
									 .Str("parentChildren", parentChildren)
									 .Pos("parentWorld", parent->world.translate)
									 .Pos("translate", transform.translate)
									 .Num("doorDistance", door->distance)
									 .Int("nodes", revealed.nodes)
									 .Int("geometries", revealed.geometries)
									 .Int("unculled", static_cast<std::int64_t>(g_unculled.size()))
									 .Int("fadesRaised", static_cast<std::int64_t>(g_fades.size()))
									 .Int("hidden", static_cast<std::int64_t>(g_hidden.size()))
									 .Pos("contentBoundCenter", content->worldBound.center)
									 .Num("contentBoundRadius", content->worldBound.radius)
									 .Raw("controls", controls)
									 .Pos("street", ground)
									 .Pos("outward", outward)
									 .Int("nearbyRefs", static_cast<std::int64_t>(nearby.size()))
									 .Str("doorwayRayHits", rayHits)
									 .Int("doorwayHidden", static_cast<std::int64_t>(doorwayHidden))
									 .Raw("doorway", doorway)
									 .Str("containerAncestry", Ancestry(g_container.get()))
									 .Raw("samples", GeometrySamples(content, 6)),
		nullptr, true);
	Hud::Show("sim.render", std::format("CLR: {} {} {} ({:.0f} m): {} geometries, hidden: doors {}, doorway {}; "
										"control doors 1-2 on the street, left to right with your back to the door",
								Hud::CellLabel(a_cell), copyMode ? "copy" : "rendered", a_mode == kRenderAligned ? "at its door" : "above its door",
								door->distance / kUnitsPerMeter, revealed.geometries, g_hidden.size() - doorwayHidden, doorwayHidden));
	return true;
}
```

Снятие скрытия и прозрачности со всего поддерева, обновление без коллизии:

```cpp
struct TreeStats
{
	std::int64_t nodes{ 0 };
	std::int64_t geometries{ 0 };
	std::int64_t culled{ 0 };
	std::int64_t fadeNodes{ 0 };
	std::int64_t fadeBelowOne{ 0 };
};

// Counts the subtree; with a_reveal also clears app-culling and makes fade nodes opaque,
// remembering what it changed (a_remember) to undo it on detach.
void Walk(RE::NiAVObject* a_object, TreeStats& a_stats, bool a_reveal, bool a_remember)
{
	++a_stats.nodes;
	if (a_object->AsGeometry()) {
		++a_stats.geometries;
	}
	if (a_object->GetAppCulled()) {
		++a_stats.culled;
		if (a_reveal) {
			a_object->SetAppCulled(false);
			if (a_remember) {
				g_unculled.emplace_back(a_object);
			}
		}
	}
	if (auto* fade = a_object->AsFadeNode()) {
		++a_stats.fadeNodes;
		auto& value = fade->GetRuntimeData().currentFade;
		if (value < 0.99f) {
			++a_stats.fadeBelowOne;
			if (a_reveal) {
				if (a_remember) {
					g_fades.emplace_back(fade, value);
				}
				value = 1.0f;
			}
		}
	}
	if (auto* node = a_object->AsNode()) {
		for (auto& child : node->GetChildren()) {
			if (child) {
				Walk(child.get(), a_stats, a_reveal, a_remember);
			}
		}
	}
}

RE::NiUpdateData NoCollisionUpdate()
{
	// kDisableCollision: do not push the new world transforms into the interior's Havok bodies.
	RE::NiUpdateData update{};
	update.flags.set(RE::NiUpdateData::Flag::kDisableCollision);
	return update;
}
```

Снятие (вызывается при `Loading Menu`, из хуков сетки и при смене состояния):

```cpp
void DetachNow(std::string_view a_why)
{
	if (!Rendering()) {
		return;
	}
	Portal::Stop();
	for (auto& object : g_unculled) {
		object->SetAppCulled(true);
	}
	for (auto& [fade, value] : g_fades) {
		fade->GetRuntimeData().currentFade = value;
	}
	const auto restored = g_unculled.size() + g_fades.size();
	g_unculled.clear();
	g_fades.clear();
	if (g_container && g_cell3D && g_cell3D->parent == g_container.get()) {
		g_container->DetachChild(g_cell3D.get());
		auto update = NoCollisionUpdate();
		g_cell3D->Update(update);  // world transforms back in the interior's own coordinates
	}
	if (auto* parent = g_container ? g_container->parent : nullptr) {
		parent->DetachChild(g_container.get());
	}
	for (auto& control : g_controls) {
		if (auto* parent = control->parent) {
			parent->DetachChild(control.get());
		}
	}
	for (auto& object : g_hidden) {
		object->SetAppCulled(false);
	}
	Trace::Write("interior.sim", Trace::Args{}
									 .Str("action", "detach")
									 .Str("why", a_why)
									 .Int("mode", g_renderMode)
									 .Int("restored", static_cast<std::int64_t>(restored)),
		nullptr, true);
	ClearWatch();
	if (g_container) {
		g_graveyard.emplace_back(g_container.get());  // copies inside it are never freed
	}
	g_container.reset();
	g_cell3D.reset();
	g_controls.clear();
	g_hidden.clear();
	g_renderMode = 0;
	g_host.reset();
	g_hostRoot = nullptr;
}

void UpdateRender(RE::TESObjectCELL* a_cell, bool a_ready, std::int64_t a_now)
{
	const auto  stage = g_stage.load();
	const auto  want = a_ready && stage >= kRenderAligned ? stage : 0u;
	const auto* loaded = a_cell ? a_cell->GetRuntimeData().loadedData : nullptr;
	const auto* current = loaded ? loaded->cell3D.get() : nullptr;
	if (Rendering() && (want != g_renderMode || g_cell3D.get() != current)) {
		DetachNow(want == 0 ? "stage or state" : "mode or 3D changed");
	}
	if (Rendering() && !Detectors::NearestDoorTo(a_cell)) {
		DetachNow("its door left the loaded cells");
	}
	if (Rendering() && (!g_host || !g_host->parent)) {
		DetachNow("its exterior cell left the scene");  // the grid hooks should have caught it first
	}
	if (want != 0 && !Rendering() && a_now >= g_nextAttachUs) {
		g_nextAttachUs = a_now + kAttachRetryUs;
		if (want == kRenderPortal) {
			AttachPortal(a_cell);
		} else {
			Attach(a_cell, want);
		}
	}
}
```

### Скрытие проёма и контрольные копии двери (стадии 3–5)

```cpp
// The street in front of the exterior door: where the player arrives when leaving the interior
// (the interior door's teleport marker), and the horizontal direction from the door to it.
// Without a marker: the door mesh's local +Y.
void Street(RE::TESObjectREFR* a_inside, const RE::NiAVObject* a_door, RE::NiPoint3& a_ground, RE::NiPoint3& a_outward)
{
	const auto* teleport = a_inside ? a_inside->extraList.GetByType<RE::ExtraTeleport>() : nullptr;
	if (const auto* data = teleport ? teleport->teleportData : nullptr) {
		a_ground = data->position;
		a_outward = a_ground - a_door->world.translate;
		a_outward.z = 0.0f;
		if (a_outward.Unitize() > 1.0f) {
			return;
		}
	}
	a_ground = a_door->world.translate;
	a_outward = { a_door->world.rotate.entry[0][1], a_door->world.rotate.entry[1][1], 0.0f };
	if (a_outward.Unitize() < 0.01f) {
		a_outward = { 0.0f, 1.0f, 0.0f };
	}
}

// References with 3D around a_point (by their position) in the scene cells and in the worldspace's
// persistent cell, plus what Havok rays from the street into the doorway hit. 0.8.4 searched the
// scene cells only and missed the Bannered Mare's building (0002CC60), hit by F6 37 units behind the door.
std::vector<RE::NiPointer<RE::TESObjectREFR>> NearbyRefs(const RE::NiPoint3& a_point, const RE::TESObjectREFR* a_skip,
	const RE::NiPoint3& a_outward, std::string& a_rayHits)
{
	auto  cells = Detectors::SceneCells();
	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* playerCell = player ? player->GetParentCell() : nullptr;
	auto* world = playerCell ? playerCell->GetRuntimeData().worldSpace : nullptr;
	if (world && world->persistentCell && std::ranges::find(cells, world->persistentCell) == cells.end()) {
		cells.push_back(world->persistentCell);
	}
	std::vector<RE::NiPointer<RE::TESObjectREFR>> refs;
	const auto add = [&](RE::TESObjectREFR* a_ref) {
		if (a_ref && a_ref != a_skip && !a_ref->As<RE::Actor>() && !a_ref->IsDisabled() && a_ref->Get3D() &&
			std::ranges::none_of(refs, [&](const auto& a_have) { return a_have.get() == a_ref; })) {
			refs.emplace_back(a_ref);
		}
	};
	for (auto* cell : cells) {
		cell->ForEachReferenceInRange(a_point, kNearbyRadius, [&](RE::TESObjectREFR* a_ref) {
			add(a_ref);
			return RE::BSContainer::ForEachResult::kContinue;
		});
	}
	for (const float height : kDoorwayRayHeights) {
		const auto   origin = a_point + a_outward * kDoorwayHalfWidth + RE::NiPoint3{ 0.0f, 0.0f, height };
		RE::NiPoint3 hit;
		if (const auto ref = Detectors::PickRef(origin, a_outward * -1.0f, hit)) {
			a_rayHits += std::format("{}{:08X}@{:.0f}", a_rayHits.empty() ? "" : ",", ref->GetFormID(), (hit - a_point).Dot(a_outward));
			add(ref.get());
		}
	}
	return refs;
}

// The largest of a_refs whose 3D bound holds a_point: the building the door is in.
RE::NiAVObject* BuildingAround(const RE::NiPoint3& a_point, const std::vector<RE::NiPointer<RE::TESObjectREFR>>& a_refs)
{
	RE::NiAVObject* best = nullptr;
	for (const auto& ref : a_refs) {
		auto* root = ref->Get3D();
		if (root && !root->GetAppCulled() && root->worldBound.center.GetDistance(a_point) < root->worldBound.radius &&
			(!best || root->worldBound.radius > best->worldBound.radius)) {
			best = root;
		}
	}
	return best;
}

// What stands in the doorway once the exterior door is hidden (24.10.2026: the player saw
// "another door" there; 0.8.4 F6 hit the building wrhousebanneredmare01.nif right behind the door,
// whose shape WRHouseBanneredMare01:16 has the door texture wrdoor01.dds). Hidden: shapes with a
// door texture whose bound holds the point just behind the door, and small shapes whose center
// is behind the door plane near its axis. All shapes near the doorway go to the trace.
std::string HideDoorway(const std::vector<RE::NiPointer<RE::TESObjectREFR>>& a_refs, const RE::NiAVObject* a_doorNode, const RE::NiPoint3& a_outward,
	std::size_t& a_hidden)
{
	struct Near
	{
		RE::BSGeometry* shape;
		RE::FormID      ref;
		std::string     model;
		float           ahead, side, up, radius;
		bool            hide{ false };
	};
	const auto         center = a_doorNode->worldBound.center;
	const auto         probe = center - a_outward * kDoorProbeDepth;
	const RE::NiPoint3 right{ a_outward.y, -a_outward.x, 0.0f };

	std::vector<Near> nearby;
	for (const auto& ref : a_refs) {
		auto*       root = ref->Get3D();
		auto*       base = ref->GetBaseObject();
		const auto* model = base ? skyrim_cast<RE::TESModel*>(base) : nullptr;
		const char* modelPath = model ? model->GetModel() : nullptr;
		RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geometry) {
			const auto  rel = a_geometry->worldBound.center - center;
			const float radius = a_geometry->worldBound.radius;
			const Near  shape{ .shape = a_geometry,
				 .ref = ref->GetFormID(),
				 .model = modelPath ? modelPath : "",
				 .ahead = rel.Dot(a_outward),  // < 0: behind the door, inside the building
				 .side = rel.Dot(right),
				 .up = rel.z,
				 .radius = radius };
			const bool holdsProbe = a_geometry->worldBound.center.GetDistance(probe) < radius;
			if (holdsProbe || (std::abs(shape.side) < kDoorwayHalfWidth + radius && std::abs(shape.up) < kDoorwayHalfHeight + radius &&
								  shape.ahead > -kDoorwayDepth - radius && shape.ahead < kDoorwayHalfWidth + radius && radius < kDoorwayMaxHiddenRadius)) {
				nearby.push_back(shape);
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}
	std::ranges::sort(nearby, {}, &Near::radius);

	std::string json;
	for (std::size_t i = 0; i < nearby.size(); ++i) {
		auto& shape = nearby[i];
		const auto texture = Detectors::TextureFile(shape.shape);
		const bool doorTexture = texture.find("door") != std::string::npos && shape.shape->worldBound.center.GetDistance(probe) < shape.radius;
		const bool smallBehind = shape.radius < kDoorwayMaxHiddenRadius && shape.ahead < -kDoorPlaneSlack && shape.ahead > -kDoorwayDepth &&
		                         std::abs(shape.side) < kDoorwayHalfWidth && std::abs(shape.up) < kDoorwayHalfHeight;
		shape.hide = (doorTexture || smallBehind) && !shape.shape->GetAppCulled();
		if (shape.hide) {
			HideShown(shape.shape);
			++a_hidden;
		}
		if (i < kMaxDoorwayTraced) {
			json += (json.empty() ? "" : ",") + Trace::Args{}
													.Form("ref", RE::TESForm::LookupByID(shape.ref))
													.Str("model", shape.model)
													.Str("shape", shape.shape->name.c_str())
													.Str("texture", texture)
													.Num("ahead", shape.ahead)
													.Num("side", shape.side)
													.Num("up", shape.up)
													.Num("radius", shape.radius)
													.Bool("hidden", shape.hide)
													.Json();
		}
	}
	return "[" + json + "]";
}

// Control copies of the exterior door on the street in front of it, numbered left to right for a
// player with the back to the door:
//   1 next to the real door, in the exterior cell's node (as the game hangs references);
//   2 in our container (as the interior).
// 0.8.4 had five: only the one in the exterior cell's node was drawn; under ObjectLODRoot, under
// ShadowSceneNode and under WorldRoot the culling process never reached them (the one registered
// with ShadowSceneNode::AttachObject left a dark door-shaped shadow, nothing else).
std::string HangControls(RE::NiAVObject* a_door, const RE::NiPoint3& a_ground, const RE::NiPoint3& a_outward)
{
	struct Place
	{
		std::string_view what;
		RE::NiNode*      parent;
		bool             light;
	};
	const std::array<Place, kControls> places{ {
		{ "next to the real door", a_door->parent, false },
		{ "in the container", g_container.get(), false },
	} };
	const RE::NiPoint3 right{ a_outward.y, -a_outward.x, 0.0f };  // the player's right, back to the door
	// as high above the street as the real door stands above it (its origin may not be at its foot)
	const float above = std::clamp(a_door->world.translate.z - a_ground.z, 0.0f, kMaxControlLift) + kControlsAboveGround;

	std::string json;
	for (std::size_t i = 0; i < places.size(); ++i) {
		const auto& place = places[i];
		const float slot = static_cast<float>(i) - static_cast<float>(places.size() - 1) / 2.0f;
		Trace::Args args;
		args.Int("number", static_cast<std::int64_t>(i + 1)).Str("what", place.what);
		auto* copy = place.parent ? CloneOf(a_door) : nullptr;
		if (copy) {
			RE::NiTransform world = a_door->world;
			world.translate = a_ground + a_outward * kControlsAhead + right * (slot * kControlsSpacing);
			world.translate.z = a_ground.z + above;
			copy->local = place.parent->world.Invert() * world;
			copy->SetAppCulled(false);
			place.parent->AttachChild(copy);
			auto update = NoCollisionUpdate();
			copy->Update(update);
			if (place.light) {
				RegisterForLight(copy);
			}
			if (place.parent != g_container.get()) {
				g_controls.emplace_back(copy);
			}
			WatchTree(std::format("door {}", i + 1), copy);
			args.Str("parent", Ancestry(place.parent)).Pos("world", copy->world.translate).Num("boundRadius", copy->worldBound.radius);
		} else {
			args.Str("parent", "none");
		}
		json += (json.empty() ? "" : ",") + args.Json();
	}
	return "[" + json + "]";
}
```

### Стадия 5: копия интерьера

```cpp
// Clones keep their Havok objects; they must never reach a physics world.
void StripCollision(RE::NiAVObject* a_object)
{
	a_object->collisionObject.reset();
	if (auto* node = a_object->AsNode()) {
		for (auto& child : node->GetChildren()) {
			if (child) {
				StripCollision(child.get());
			}
		}
	}
}

RE::NiAVObject* CloneOf(RE::NiAVObject* a_source)
{
	auto* clone = netimmerse_cast<RE::NiAVObject*>(a_source->Clone());
	if (clone) {
		g_graveyard.emplace_back(clone);
		StripCollision(clone);
	}
	return clone;
}

// Copy mode: clones of the interior's references except actors (cloned skeletons crashed 0.8.2)
// and the door we look through, under one node in the interior's coordinates.
RE::NiNode* CopyInterior(RE::TESObjectCELL* a_cell, const RE::TESObjectREFR* a_skip, std::size_t& a_copied)
{
	auto* copy = RE::NiNode::Create(0);
	g_graveyard.emplace_back(copy);
	copy->name = "CLR InteriorSim copy";
	std::vector<RE::NiPointer<RE::NiAVObject>> roots;
	a_cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
		if (a_ref && a_ref != a_skip && !a_ref->As<RE::Actor>() && !a_ref->IsDisabled()) {
			if (auto* root = a_ref->Get3D()) {
				roots.emplace_back(root);
			}
		}
		return RE::BSContainer::ForEachResult::kContinue;
	});
	for (auto& root : roots) {
		if (auto* clone = CloneOf(root.get())) {
			clone->local = root->local;  // under the cell's per-type nodes, which sit at the origin
			copy->AttachChild(clone, true);
			++a_copied;
		}
	}
	return copy;
}

// ShadowSceneNode::AttachObject (AE#106330) queues an object; AE#106335 later walks its geometry
// and registers it with the lights, then empties the queue (no detach call exists). Lighting
// shaders draw only with their light data, so unregistered geometry may never be drawn.
void RegisterForLight(RE::NiAVObject* a_object)
{
	if (auto* ssn = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0]) {
		ssn->AttachObject(a_object);
	}
}
```

Константы и состояние этих стадий — в начале архивного `InteriorSim.cpp`:
- `kRenderAligned = 3`, `kRenderLifted = 4`, `kRenderCopyLifted = 5`;
- `kControls*`, `kDoorway*`;
- `g_container`, `g_controls`, `g_unculled`, `g_fades`, `g_graveyard`.
