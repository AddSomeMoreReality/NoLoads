#include "PortalState.h"

namespace Portal::detail
{
	bool                                        g_mainDumpRequested{ false };
	std::uint32_t                               g_mainDumps{ 0 };
	std::uint64_t               g_imageSpaceHeld{ 0 };  // swaps still in place after RenderPlayerView

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

	// Every frame, before the passes: the swap of the last frame goes back unless the game wrote its own
	// values since, and inside an interior its image space is kept for the swap.
	void LookTick()
	{
		auto* manager = RE::ImageSpaceManager::GetSingleton();
		if (!manager) {
			return;
		}
		auto& data = manager->GetImageSpaceData();
		if (g_gameImageSpace) {
			if (g_interiorLook && std::memcmp(&data, &g_interiorLook->data, sizeof(data)) == 0) {
				data = *g_gameImageSpace;
			}
			g_gameImageSpace.reset();
		}
		auto*      player = RE::PlayerCharacter::GetSingleton();
		auto*      cell = player ? player->GetParentCell() : nullptr;
		auto*      ui = RE::UI::GetSingleton();
		const bool transition = !ui || ui->IsMenuOpen(RE::FaderMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
		if (cell && cell->IsInteriorCell() && !g_active && !transition) {
			g_interiorLook = InteriorLook{ cell->GetFormID(), data };
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

	void ToggleInteriorImageSpace()
	{
		g_interiorImageSpace = !g_interiorImageSpace;
		Trace::Write("portal.imageSpace", Trace::Args{}
											   .Bool("on", g_interiorImageSpace)
											   .Bool("portal", g_active)
											   .FormID("captured", g_interiorLook ? g_interiorLook->cell : 0)
											   .Raw("game", ImageSpaceJson()),
			nullptr, true);
		Hud::Show("portal", g_interiorImageSpace && !g_interiorLook ?
		                        "CLR: interior image space through the portal on, but not captured yet: step inside once" :
		                        std::format("CLR: interior image space through the portal {}", g_interiorImageSpace ? "on" : "off"));
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
			.Bool("swapOn", g_interiorImageSpace)
			.Int("swaps", static_cast<std::int64_t>(g_imageSpaceSwaps))
			.Int("held", static_cast<std::int64_t>(g_imageSpaceHeld))
			.Json();
	}

	void RequestDump()
	{
		g_dumpRequested = g_active;
		g_mainDumpRequested = true;
	}

	void AfterPlayerView()
	{
		if (g_gameImageSpace && g_interiorLook) {
			auto* imageSpaces = RE::ImageSpaceManager::GetSingleton();
			if (imageSpaces && std::memcmp(&imageSpaces->GetImageSpaceData(), &g_interiorLook->data, sizeof(RE::ImageSpaceData)) == 0) {
				++g_imageSpaceHeld;  // the game did not write its own values between our pass and the image space
			}
		}
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
