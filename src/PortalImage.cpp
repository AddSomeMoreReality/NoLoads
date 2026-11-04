#include "PortalState.h"

namespace Portal::detail
{
	bool                      g_mainDumpRequested{ false };
	std::uint32_t             g_mainDumps{ 0 };
	std::atomic<std::int64_t> g_snapshotWatchedUntilUs{ 0 };  // Trace::NowUs: the watchdog watches F11's snapshots until then

	constexpr std::int64_t kSnapshotWatchUs = 10'000'000;

	RE::BSFogProperty* WorldFog()
	{
		auto* scene = WorldScene();
		return scene ? scene->GetRuntimeData().fogProperty.get() : nullptr;
	}

	FogValues ReadFog(const RE::BSFogProperty& a_fog)
	{
		return { .nearColor = a_fog.nearColor,
			.farColor = a_fog.farColor,
			.nearDistance = a_fog.nearDistance,
			.farDistance = a_fog.farDistance,
			.power = a_fog.power,
			.clamp = a_fog.clamp,
			.source = "fog property" };
	}

	void WriteFog(RE::BSFogProperty& a_fog, const FogValues& a_values)
	{
		a_fog.nearColor = a_values.nearColor;
		a_fog.farColor = a_values.farColor;
		a_fog.nearDistance = a_values.nearDistance;
		a_fog.farDistance = a_values.farDistance;
		a_fog.power = a_values.power;
		a_fog.clamp = a_values.clamp;
	}

	std::string FogValuesJson(const FogValues& a_fog)
	{
		const auto color = [](const RE::NiColor& a_color) { return std::format("[{:.3f},{:.3f},{:.3f}]", a_color.red, a_color.green, a_color.blue); };
		return Trace::Args{}
			.Str("source", a_fog.source)
			.Raw("nearColor", color(a_fog.nearColor))
			.Raw("farColor", color(a_fog.farColor))
			.Num("near", a_fog.nearDistance)
			.Num("far", a_fog.farDistance)
			.Num("power", a_fog.power)
			.Num("clamp", a_fog.clamp)
			.Json();
	}

	// The interior's fog: the fog property as the game set it while the player stood in that cell, or else the
	// cell's lighting data (XCLL) with what it inherits from its lighting template.
	std::optional<FogValues> InteriorFogOf(const RE::TESObjectCELL* a_cell)
	{
		if (!a_cell) {
			return std::nullopt;
		}
		if (g_interiorFog && g_interiorFog->cell == a_cell->GetFormID()) {
			auto fog = g_interiorFog->fog;
			fog.source = "captured inside";
			return fog;
		}
		auto*       cell = const_cast<RE::TESObjectCELL*>(a_cell);
		const auto* own = cell->GetLighting();
		if (!own) {
			return std::nullopt;
		}
		const auto* lightingTemplate = cell->GetRuntimeData().lightingTemplate;
		const auto  inherit = [&](RE::INTERIOR_DATA::Inherit a_what) -> const RE::INTERIOR_DATA& {
            return lightingTemplate && own->lightingTemplateInheritanceFlags.any(a_what) ? lightingTemplate->data : *own;
		};
		const auto  color = [](const RE::Color& a_color) { return RE::NiColor{ a_color.red / 255.0f, a_color.green / 255.0f, a_color.blue / 255.0f }; };
		const auto& colors = inherit(RE::INTERIOR_DATA::Inherit::kFogColor);
		const auto  clamp = inherit(RE::INTERIOR_DATA::Inherit::kFogMax).fogClamp;
		return FogValues{ .nearColor = color(colors.fogColorNear),
			.farColor = color(colors.fogColorFar),
			.nearDistance = inherit(RE::INTERIOR_DATA::Inherit::kFogNear).fogNear,
			.farDistance = inherit(RE::INTERIOR_DATA::Inherit::kFogFar).fogFar,
			.power = inherit(RE::INTERIOR_DATA::Inherit::kFogPower).fogPower,
			.clamp = clamp > 0.0f ? clamp : 1.0f,
			.source = lightingTemplate ? "cell lighting and template" : "cell lighting" };
	}

	void UseInteriorLook(SavedLook& a_saved)
	{
		using GetAmbient = void (*)(RE::TESObjectCELL*, float*, float*, float*);
		using SetAmbient = void (*)(const float*, const float*, float);
		static REL::Relocation<GetAmbient> getAmbient{ REL::ID(18966) };
		static REL::Relocation<SetAmbient> setAmbient{ REL::ID(105643) };

		auto* ambient = reinterpret_cast<std::byte*>(&RE::BSShaderManager::State::GetSingleton()) + kAmbientOffset;
		std::memcpy(a_saved.ambient.data(), ambient, kAmbientBytes);
		alignas(16) float colors[24]{};  // six directional colors (0x48 bytes in MistMenu), with room to spare
		alignas(16) float specular[8]{};
		float             fresnel = 0.0f;
		getAmbient(g_door.cell, colors, specular, &fresnel);
		setAmbient(colors, specular, fresnel);

		auto*       scene = WorldScene();
		auto*       sun = scene ? scene->GetRuntimeData().sunLight : nullptr;
		const auto* lighting = g_door.cell->GetLighting();
		if (sun && sun->light && lighting) {
			auto& data = sun->light->GetLightRuntimeData();
			a_saved.sun = data.diffuse;
			a_saved.sunChanged = true;
			data.diffuse = { lighting->directional.red / 255.0f, lighting->directional.green / 255.0f, lighting->directional.blue / 255.0f };
		}

		// T12, layer 1: the effects of the pass (fire, smoke, candles) take their fog from the world scene's fog
		// property (AE#107524 calls AE#105637: State.shadowSceneNode[sceneGraph] + 0x220), the street's.
		auto* fog = WorldFog();
		if (g_gradeStages >= 1 && fog) {
			if (const auto interior = InteriorFogOf(g_door.cell)) {
				a_saved.fog = ReadFog(*fog);
				a_saved.fogChanged = true;
				WriteFog(*fog, *interior);
			}
		}
	}

	void RestoreLook(const SavedLook& a_saved)
	{
		auto* ambient = reinterpret_cast<std::byte*>(&RE::BSShaderManager::State::GetSingleton()) + kAmbientOffset;
		std::memcpy(ambient, a_saved.ambient.data(), kAmbientBytes);
		auto* scene = WorldScene();
		auto* sun = scene ? scene->GetRuntimeData().sunLight : nullptr;
		if (a_saved.sunChanged && sun && sun->light) {
			sun->light->GetLightRuntimeData().diffuse = a_saved.sun;
		}
		if (auto* fog = WorldFog(); a_saved.fogChanged && fog) {
			WriteFog(*fog, a_saved.fog);
		}
	}

	std::uint32_t BytesPerPixel(DXGI_FORMAT a_format)
	{
		switch (a_format) {
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return 8;
		case DXGI_FORMAT_R11G11B10_FLOAT:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R10G10B10A2_UNORM:
			return 4;
		default:
			return 0;
		}
	}

	// DDS (DX10 header) of the top left a_width x a_height of a texture, read back through a staging copy:
	// the GPU waits for it.
	void WriteDump(ID3D11Texture2D* a_texture, std::uint32_t a_width, std::uint32_t a_height, std::string_view a_name, std::uint32_t a_number)
	{
		auto* device = Device();
		auto* context = Context();
		if (!device || !context || !a_texture) {
			return;
		}
		D3D11_TEXTURE2D_DESC desc{};
		a_texture->GetDesc(&desc);
		const auto pixelBytes = BytesPerPixel(desc.Format);
		if (pixelBytes == 0 || desc.SampleDesc.Count != 1) {
			Trace::Write("portal.dump", Trace::Args{}.Str("name", a_name).Int("format", desc.Format).Int("samples", desc.SampleDesc.Count).Str("skipped", "format"), nullptr, true);
			return;
		}
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.MiscFlags = 0;
		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ID3D11Texture2D* staging = nullptr;
		if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) {
			return;
		}
		context->CopySubresourceRegion(staging, 0, 0, 0, 0, a_texture, 0, nullptr);
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
			staging->Release();
			return;
		}
		const std::uint32_t width = (std::min)(a_width, desc.Width);
		const std::uint32_t height = (std::min)(a_height, desc.Height);
		bool                fallback = false;
		const auto          path = Settings::Get().ResolveLogDirectory(fallback) / std::format("{}-{}.dds", a_name, a_number);
		if (std::ofstream file{ path, std::ios::binary }) {
			const std::uint32_t rowBytes = width * pixelBytes;
			std::uint32_t       header[32]{};
			header[0] = 124;
			header[1] = 0x100F;  // caps, height, width, pitch, pixel format
			header[2] = height;
			header[3] = width;
			header[4] = rowBytes;
			header[18] = 32;   // pixel format size
			header[19] = 0x4;  // fourCC
			header[20] = 'D' | ('X' << 8) | ('1' << 16) | ('0' << 24);
			header[26] = 0x1000;  // caps: texture
			const std::uint32_t dx10[5]{ static_cast<std::uint32_t>(desc.Format), 3 /* texture 2D */, 0, 1, 0 };
			file.write("DDS ", 4);
			file.write(reinterpret_cast<const char*>(header), 124);
			file.write(reinterpret_cast<const char*>(dx10), sizeof(dx10));
			for (std::uint32_t row = 0; row < height; ++row) {
				file.write(static_cast<const char*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch, rowBytes);
			}
		}
		context->Unmap(staging, 0);
		staging->Release();
		auto*      player = RE::PlayerCharacter::GetSingleton();
		const auto format = desc.Format;
		Trace::Write("portal.dump", Trace::Args{}
										.Str("name", a_name)
										.Str("path", path.string())
										.Int("width", width)
										.Int("height", height)
										.Int("format", format)
										.Cell("playerCell", player ? player->GetParentCell() : nullptr)
										.Bool("portal", g_active)
										.Int("gradeStages", g_gradeStages)
										.Raw("imageSpace", Portal::ImageSpaceJson()),
			nullptr, true);
	}

	std::string ImageSpaceDataJson(const RE::ImageSpaceData& a_data)
	{
		const auto& hdr = a_data.baseData.hdr;
		const auto& cinematic = a_data.baseData.cinematic;
		const auto& tint = a_data.baseData.tint;
		return Trace::Args{}
			.Raw("hdr", std::format("[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]", hdr.eyeAdaptSpeed, hdr.bloomBlurRadius, hdr.bloomThreshold,
							hdr.bloomScale, hdr.receiveBloomThreshold, hdr.white, hdr.sunlightScale, hdr.skyScale, hdr.eyeAdaptStrength))
			.Raw("cinematic", std::format("[{:.3f},{:.3f},{:.3f}]", cinematic.saturation, cinematic.brightness, cinematic.contrast))
			.Raw("tint", std::format("[{:.3f},{:.3f},{:.3f},{:.3f}]", tint.amount, tint.color.red, tint.color.green, tint.color.blue))
			.Num("fade", a_data.modData.data[RE::ImageSpaceModData::kFadeAmount])
			.Num("baseAmount", a_data.baseAmount)
			.Num("modAmount", a_data.modAmount)
			.Json();
	}

	// Every frame (PlayerCharacter::Update): inside an interior, without the portal and no transition on, keep
	// its image space and its fog as the game set them, for the portal of that cell. Until 0.8.26 the tick also
	// put back the image space F2 had written for the whole frame; that switch is gone (T12).
	void LookTick()
	{
		auto* manager = RE::ImageSpaceManager::GetSingleton();
		if (!manager) {
			return;
		}
		auto*      player = RE::PlayerCharacter::GetSingleton();
		auto*      cell = player ? player->GetParentCell() : nullptr;
		auto*      ui = RE::UI::GetSingleton();
		const bool transition = !ui || ui->IsMenuOpen(RE::FaderMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
		if (cell && cell->IsInteriorCell() && !g_active && !transition) {
			g_interiorLook = InteriorLook{ cell->GetFormID(), manager->GetImageSpaceData() };
			if (const auto* fog = WorldFog()) {
				g_interiorFog = InteriorFog{ cell->GetFormID(), ReadFog(*fog) };
			}
		}
	}
}

namespace Portal
{
	void ToggleTemporalAA()
	{
		// ImageSpaceManager (a pointer: the game loads AE#414660 with mov) + 0x1F0 -> TAA shaders, + 0x18
		// taaEnabled. The game reads it while it runs (AE#52727, AE#23157); bUseTAA:Display, SkyrimPrefs.ini
		// is not touched.
		auto* manager = RE::ImageSpaceManager::GetSingleton();
		auto* taa = manager ? manager->GetRuntimeData().BSImagespaceShaderISTemporalAA : nullptr;
		if (!taa) {
			Hud::Show("portal", "CLR: TAA switch not found");
			return;
		}
		taa->taaEnabled = !taa->taaEnabled;
		Trace::Write("portal.taa", Trace::Args{}.Bool("taaEnabled", taa->taaEnabled).Bool("portal", g_active), nullptr, true);
		Hud::Show("portal", std::format("CLR: temporal antialiasing (TAA) {}", taa->taaEnabled ? "on" : "off"));
	}

	void TogglePortalGrading()
	{
		constexpr std::array<std::string_view, 3> kNames{ "off", "interior composite", "interior composite and look" };
		ReadGradeStages();
		g_gradeStages = (g_gradeStages + 1) % 3;
		Trace::Write("portal.grade", Trace::Args{}.Str("event", "toggle").Int("stages", g_gradeStages).Bool("portal", g_active), nullptr, true);
		RequestGradeTrace("toggle");
		Hud::Show("portal", std::format("CLR: portal grading {} ({})", g_gradeStages, kNames[g_gradeStages]));
	}

	std::string ImageSpaceJson()
	{
		auto* manager = RE::ImageSpaceManager::GetSingleton();
		if (!manager) {
			return "null";
		}
		// hdr: eyeAdaptSpeed, bloomBlurRadius, bloomThreshold, bloomScale, receiveBloomThreshold, white,
		// sunlightScale, skyScale, eyeAdaptStrength; cinematic: saturation, brightness, contrast; tint: amount, rgb
		return Trace::Args{}
			.Raw("now", ImageSpaceDataJson(manager->GetImageSpaceData()))
			.Raw("interior", g_interiorLook ? ImageSpaceDataJson(g_interiorLook->data) : "null")
			.FormID("interiorCell", g_interiorLook ? g_interiorLook->cell : 0)
			.Json();
	}

	std::string FogJson(const RE::TESObjectCELL* a_cell)
	{
		const auto* fog = WorldFog();
		const auto  interior = InteriorFogOf(a_cell);
		std::string unnamed;
		if (fog) {
			// BSFogProperty + 0x58 ... 0x83: fields NG leaves unnamed (T13 looked for colours there)
			const auto* words = reinterpret_cast<const float*>(reinterpret_cast<const std::byte*>(fog) + 0x58);
			for (int i = 0; i < 11; ++i) {
				unnamed += std::format("{}{}", i ? "," : "", std::isfinite(words[i]) ? std::format("{:.3f}", words[i]) : "null");  // word 0 was nan in 0.8.28
			}
		}
		return Trace::Args{}
			.Raw("world", fog ? FogValuesJson(ReadFog(*fog)) : "null")
			.Raw("worldUnnamed58", fog ? "[" + unnamed + "]" : "null")
			.Num("invFrameBufferRange", RE::BSShaderManager::State::GetSingleton().invFrameBufferRange)
			.FormID("cell", a_cell ? a_cell->GetFormID() : 0)
			.Raw("cellFog", interior ? FogValuesJson(*interior) : "null")
			.Json();
	}

	bool SnapshotWatched()
	{
		return Trace::NowUs() < g_snapshotWatchedUntilUs.load(std::memory_order_relaxed);
	}

	void RequestDump()
	{
		g_snapshotWatchedUntilUs.store(Trace::NowUs() + kSnapshotWatchUs, std::memory_order_relaxed);
		g_dumpRequested = g_active;
		g_mainDumpRequested = true;
		RequestHdrDumps();
		RequestGradeTrace("snapshot");
	}

	void AfterPlayerView()
	{
		if (!g_mainDumpRequested) {
			return;
		}
		g_mainDumpRequested = false;
		auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
		auto* texture = renderer ? reinterpret_cast<ID3D11Texture2D*>(renderer->GetRuntimeData().renderTargets[RenderTarget::kMAIN].texture) : nullptr;
		if (texture) {
			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			WriteDump(texture, desc.Width, desc.Height, "main", ++g_mainDumps);
		}
	}
}
