#include "TSICWebGPUDriver.h"
#include "TSICWebShaders.h"
#include "TSICWebUI.h"

#include "PipelineStateCache.h"
#include "RHICommandList.h"
#include "RHIDefinitions.h"
#include "RHIResources.h"
#include "RHIStaticStates.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RenderUtils.h"
#include "SceneUtils.h"
#include "ScreenRendering.h"
#include "ShaderParameterStruct.h"
#include "GlobalShader.h"

THIRD_PARTY_INCLUDES_START
#include <Ultralight/Bitmap.h>
THIRD_PARTY_INCLUDES_END

namespace TSICWebUI
{

FWebGPUDriver::FWebGPUDriver()
{
	UE_LOG(LogTSICWebUI, Log, TEXT("FWebGPUDriver created"));
}

FWebGPUDriver::~FWebGPUDriver()
{
	FlushRenderingCommands();
	Textures.Empty();
	RenderBuffers.Empty();
	Geometries.Empty();
	UE_LOG(LogTSICWebUI, Log, TEXT("FWebGPUDriver destroyed"));
}

void FWebGPUDriver::BeginSynchronize()
{
	static int32 SyncCount = 0;
	if (++SyncCount == 1 || SyncCount == 60 || SyncCount == 300)
	{
		UE_LOG(LogTSICWebUI, Log, TEXT("[gpu] BeginSynchronize #%d"), SyncCount);
	}
}
void FWebGPUDriver::EndSynchronize() {}

uint32_t FWebGPUDriver::NextTextureId()      { return NextTexId++; }
uint32_t FWebGPUDriver::NextRenderBufferId() { return NextRBId++;  }
uint32_t FWebGPUDriver::NextGeometryId()     { return NextGeoId++; }

void FWebGPUDriver::CreateTexture(uint32_t texture_id, ultralight::RefPtr<ultralight::Bitmap> bitmap)
{
	FResourceWork W;
	W.Kind  = EResourceWorkKind::CreateTexture;
	W.Id    = texture_id;
	W.Width = bitmap->width();
	W.Height = bitmap->height();
	W.bIsRenderTarget = bitmap->IsEmpty();

	W.PixelFormat = PF_B8G8R8A8;
	if (!bitmap->IsEmpty() && bitmap->format() == ultralight::BitmapFormat::A8_UNORM)
	{
		W.PixelFormat = PF_A8;
	}

	W.TexFlags = ETextureCreateFlags::ShaderResource;
	if (W.bIsRenderTarget)
	{
		W.TexFlags |= ETextureCreateFlags::RenderTargetable;
	}

	if (!bitmap->IsEmpty())
	{
		W.RowBytes = bitmap->row_bytes();
		W.PixelData.SetNumUninitialized(W.RowBytes * W.Height);
		void* Pixels = bitmap->LockPixels();
		if (Pixels)
		{
			FMemory::Memcpy(W.PixelData.GetData(), Pixels, W.RowBytes * W.Height);
		}
		bitmap->UnlockPixels();
	}

	static int32 LogCount = 0;
	if (++LogCount <= 8 || (LogCount % 50) == 0)
	{
		UE_LOG(LogTSICWebUI, Log, TEXT("[gpu] CreateTexture id=%u %ux%u RT=%d (#%d)"),
			texture_id, W.Width, W.Height, W.bIsRenderTarget ? 1 : 0, LogCount);
	}

	FScopeLock Lock(&WorkLock);
	PendingWork.Add(MoveTemp(W));
}

void FWebGPUDriver::UpdateTexture(uint32_t texture_id, ultralight::RefPtr<ultralight::Bitmap> bitmap)
{
	if (bitmap->IsEmpty())
	{
		return;
	}

	FResourceWork W;
	W.Kind   = EResourceWorkKind::UpdateTexture;
	W.Id     = texture_id;
	W.Width  = bitmap->width();
	W.Height = bitmap->height();
	W.RowBytes = bitmap->row_bytes();

	W.PixelData.SetNumUninitialized(W.RowBytes * W.Height);
	void* Pixels = bitmap->LockPixels();
	if (Pixels)
	{
		FMemory::Memcpy(W.PixelData.GetData(), Pixels, W.RowBytes * W.Height);
	}
	bitmap->UnlockPixels();

	FScopeLock Lock(&WorkLock);
	PendingWork.Add(MoveTemp(W));
}

void FWebGPUDriver::DestroyTexture(uint32_t texture_id)
{
	// Defer the internal/external decision to ApplyResourceWork — it owns the
	// Textures map and knows bIsExternal. External destroys are no-ops there;
	// the only real removal happens via UnregisterExternalTexture.
	FResourceWork W;
	W.Kind = EResourceWorkKind::DestroyTexture;
	W.Id   = texture_id;
	FScopeLock Lock(&WorkLock);
	PendingWork.Add(MoveTemp(W));
}

uint32 FWebGPUDriver::RegisterExternalTexture(FTextureRHIRef RHI, uint32 Width, uint32 Height)
{
	if (!RHI.IsValid() || Width == 0 || Height == 0)
	{
		UE_LOG(LogTSICWebUI, Warning,
			TEXT("[gpu] RegisterExternalTexture: rejected invalid input (RHIValid=%d, %ux%u)"),
			RHI.IsValid() ? 1 : 0, Width, Height);
		return 0;
	}

	const uint32 NewId = NextTexId++;

	// Publish to the game-thread-visible mirror immediately so callers that
	// look the id up via GetRHITexture before the next ExecuteCommands tick
	// (e.g., the slate brush wiring) see the texture.
	{
		FScopeLock Lock(&ExposedTexLock);
		ExposedTextures.Add(NewId, RHI);
	}

	// Send the render-thread-owned Textures map an "insert external" intent so
	// the draw loop can resolve the id when Ultralight references it. The
	// ExternalTextureRHI field tells ApplyResourceWork to passthrough instead of
	// allocating.
	FResourceWork W;
	W.Kind   = EResourceWorkKind::CreateTexture;
	W.Id     = NewId;
	W.Width  = Width;
	W.Height = Height;
	W.bIsRenderTarget    = false;
	W.TexFlags           = ETextureCreateFlags::ShaderResource;
	W.ExternalTextureRHI = RHI;
	{
		FScopeLock Lock(&WorkLock);
		PendingWork.Add(MoveTemp(W));
	}
	UE_LOG(LogTSICWebUI, Log, TEXT("[gpu] RegisterExternalTexture id=%u %ux%u"), NewId, Width, Height);
	return NewId;
}

void FWebGPUDriver::UnregisterExternalTexture(uint32 TextureId)
{
	bool bRemovedFromExposed = false;
	{
		FScopeLock Lock(&ExposedTexLock);
		bRemovedFromExposed = ExposedTextures.Remove(TextureId) > 0;
	}
	if (!bRemovedFromExposed)
	{
		return;
	}

	FResourceWork W;
	W.Kind = EResourceWorkKind::ExternalUnregister;
	W.Id   = TextureId;
	{
		FScopeLock Lock(&WorkLock);
		PendingWork.Add(MoveTemp(W));
	}
	UE_LOG(LogTSICWebUI, Log, TEXT("[gpu] UnregisterExternalTexture id=%u"), TextureId);
}

void FWebGPUDriver::CreateRenderBuffer(uint32_t render_buffer_id, const ultralight::RenderBuffer& buffer)
{
	FResourceWork W;
	W.Kind            = EResourceWorkKind::CreateRenderBuffer;
	W.Id              = render_buffer_id;
	W.BoundTextureId  = buffer.texture_id;
	W.Width           = buffer.width;
	W.Height          = buffer.height;
	FScopeLock Lock(&WorkLock);
	PendingWork.Add(MoveTemp(W));
}

void FWebGPUDriver::DestroyRenderBuffer(uint32_t render_buffer_id)
{
	FResourceWork W;
	W.Kind = EResourceWorkKind::DestroyRenderBuffer;
	W.Id   = render_buffer_id;
	FScopeLock Lock(&WorkLock);
	PendingWork.Add(MoveTemp(W));
}

void FWebGPUDriver::CreateGeometry(uint32_t geometry_id,
	const ultralight::VertexBuffer& vertices, const ultralight::IndexBuffer& indices)
{
	FResourceWork W;
	W.Kind         = EResourceWorkKind::CreateGeometry;
	W.Id           = geometry_id;
	W.VertexFormat = vertices.format;
	W.VertexData.Append(static_cast<const uint8*>(vertices.data), vertices.size);
	W.IndexData .Append(static_cast<const uint8*>(indices.data),  indices.size);
	FScopeLock Lock(&WorkLock);
	PendingWork.Add(MoveTemp(W));
}

void FWebGPUDriver::UpdateGeometry(uint32_t geometry_id,
	const ultralight::VertexBuffer& vertices, const ultralight::IndexBuffer& indices)
{
	FResourceWork W;
	W.Kind         = EResourceWorkKind::UpdateGeometry;
	W.Id           = geometry_id;
	W.VertexFormat = vertices.format;
	W.VertexData.Append(static_cast<const uint8*>(vertices.data), vertices.size);
	W.IndexData .Append(static_cast<const uint8*>(indices.data),  indices.size);
	FScopeLock Lock(&WorkLock);
	PendingWork.Add(MoveTemp(W));
}

void FWebGPUDriver::DestroyGeometry(uint32_t geometry_id)
{
	FResourceWork W;
	W.Kind = EResourceWorkKind::DestroyGeometry;
	W.Id   = geometry_id;
	FScopeLock Lock(&WorkLock);
	PendingWork.Add(MoveTemp(W));
}

void FWebGPUDriver::UpdateCommandList(const ultralight::CommandList& list)
{
	FScopeLock Lock(&CommandLock);
	PendingCommands.SetNumUninitialized(list.size);
	if (list.size > 0)
	{
		FMemory::Memcpy(PendingCommands.GetData(), list.commands,
			list.size * sizeof(ultralight::Command));
	}
}

namespace
{
	FMatrix44f UlMatrixToUE(const ultralight::Matrix4x4& M)
	{
		// Ultralight's float[16] is column-major OpenGL layout: M[col*4+row].
		// HLSL cbuffers default to column-major, so we want HLSL's Transform to
		// expose those same columns. FMatrix44f stores ROW-major; we transpose
		// at upload so the row-major host matches the column-major HLSL view.
		const auto& D = M.data;
		return FMatrix44f(
			FPlane4f(D[0], D[4], D[8],  D[12]),
			FPlane4f(D[1], D[5], D[9],  D[13]),
			FPlane4f(D[2], D[6], D[10], D[14]),
			FPlane4f(D[3], D[7], D[11], D[15]));
	}

	// Match AppCore's ApplyProjection: build a 2D orthographic projection that
	// maps (0..W, 0..H) page pixels to (-1..+1, +1..-1) clip space, then
	// premultiply by Ultralight's state.transform. Result is the final
	// model-view-projection matrix the shader expects in Transform.
	FMatrix44f BuildMVP(const ultralight::Matrix4x4& UlTransform, float W, float H)
	{
		// Ortho projection (HLSL column-major view; we still store row-major UE).
		// Column 0: (2/W, 0, 0, 0)
		// Column 1: (0, -2/H, 0, 0)
		// Column 2: (0, 0, 1, 0)
		// Column 3: (-1, 1, 0, 1)
		const FMatrix44f Ortho(
			FPlane4f(2.f / W, 0.f,       0.f, -1.f),
			FPlane4f(0.f,    -2.f / H,   0.f,  1.f),
			FPlane4f(0.f,     0.f,       1.f,  0.f),
			FPlane4f(0.f,     0.f,       0.f,  1.f));
		const FMatrix44f T = UlMatrixToUE(UlTransform);

		// HLSL view of these is column-major; HLSL mul(M, v) = M*v, and we want
		// MVP = Ortho * Transform on the vector. With row-major hosts of two
		// matrices that HLSL re-interprets as column-major, the product to send
		// is M_host_result = T_host * Ortho_host so that HLSL sees
		// Ortho_col * T_col (= equivalent of host's row-major (Ortho * T)).
		// In practice the host multiplication that lands as the column-major
		// "Ortho * T" the shader needs is T_host * Ortho_host.
		return T * Ortho;
	}

	// Populate the full FTSICWebFillVS::FParameters struct from an Ultralight
	// GPUState. Matches the cbuffer layout in TSICWebCommon.ush.
	void FillShaderParams(
		FTSICWebFillVS::FParameters& Params,
		const ultralight::GPUState& State,
		TMap<uint32, TSICWebUI::FGPUTextureEntry>& Textures)
	{
		const float VW = static_cast<float>(FMath::Max<uint32>(State.viewport_width, 1));
		const float VH = static_cast<float>(FMath::Max<uint32>(State.viewport_height, 1));
		Params.State = FVector4f(0.f, VW, VH, 1.f);
		Params.Transform = BuildMVP(State.transform, VW, VH);

		// Ultralight 1.4 GPUState has no integer uniforms; we still bind the slot.
		Params.Integer4[0] = FIntVector4(0, 0, 0, 0);
		Params.Integer4[1] = FIntVector4(0, 0, 0, 0);

		// Pack 8 floats into 2 vec4s.
		Params.Scalar4[0] = FVector4f(
			State.uniform_scalar[0], State.uniform_scalar[1],
			State.uniform_scalar[2], State.uniform_scalar[3]);
		Params.Scalar4[1] = FVector4f(
			State.uniform_scalar[4], State.uniform_scalar[5],
			State.uniform_scalar[6], State.uniform_scalar[7]);

		for (int32 i = 0; i < 8; ++i)
		{
			Params.Vector[i] = FVector4f(
				State.uniform_vector[i].x,
				State.uniform_vector[i].y,
				State.uniform_vector[i].z,
				State.uniform_vector[i].w);
		}

		Params.ClipData = FIntVector4(State.clip_size, 0, 0, 0);
		for (int32 i = 0; i < 8; ++i)
		{
			Params.Clip[i] = UlMatrixToUE(State.clip[i]);
		}

		// Texture bindings — slot 0 takes texture_1_id, slot 1 takes texture_2_id
		// (matches AppCore's HLSL Texture0/Texture1 conventions). Null IDs fall
		// back to GBlackTexture so the shader param struct never sees nullptr.
		FRHITexture* DefaultTex = GBlackTexture && GBlackTexture->TextureRHI
			? GBlackTexture->TextureRHI.GetReference()
			: nullptr;
		auto BindTexture = [&Textures, DefaultTex](uint32 TexId) -> FRHITexture*
		{
			if (TexId == 0) return DefaultTex;
			const TSICWebUI::FGPUTextureEntry* Entry = Textures.Find(TexId);
			return (Entry && Entry->TextureRHI.IsValid())
				? Entry->TextureRHI.GetReference()
				: DefaultTex;
		};
		Params.Texture0 = BindTexture(State.texture_1_id);
		Params.Texture1 = BindTexture(State.texture_2_id);
		Params.Sampler0 = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}
}

void FWebGPUDriver::ApplyResourceWork(FRHICommandListImmediate& RHICmdList, TArray<FResourceWork>& Work)
{
	for (FResourceWork& W : Work)
	{
		switch (W.Kind)
		{
		case EResourceWorkKind::CreateTexture:
		{
			FGPUTextureEntry Entry;
			Entry.Width  = W.Width;
			Entry.Height = W.Height;
			Entry.bIsRenderTarget = W.bIsRenderTarget;

			if (W.ExternalTextureRHI.IsValid())
			{
				// Externally-owned texture: passthrough, don't allocate.
				Entry.TextureRHI  = W.ExternalTextureRHI;
				Entry.bIsExternal = true;
			}
			else
			{
				FRHITextureCreateDesc Desc =
					FRHITextureCreateDesc::Create2D(TEXT("TSICWebUITexture"), W.Width, W.Height, W.PixelFormat)
					.SetFlags(W.TexFlags)
					.SetNumMips(1);
				Entry.TextureRHI = RHICmdList.CreateTexture(Desc);
				if (W.PixelData.Num() > 0 && Entry.TextureRHI.IsValid())
				{
					FUpdateTextureRegion2D Region(0, 0, 0, 0, W.Width, W.Height);
					RHIUpdateTexture2D(Entry.TextureRHI, 0, Region, W.RowBytes, W.PixelData.GetData());
				}
			}

			if (Entry.TextureRHI.IsValid())
			{
				FScopeLock Lock(&ExposedTexLock);
				ExposedTextures.Add(W.Id, Entry.TextureRHI);
			}
			Textures.Add(W.Id, MoveTemp(Entry));
			break;
		}

		case EResourceWorkKind::UpdateTexture:
		{
			FGPUTextureEntry* Entry = Textures.Find(W.Id);
			if (!Entry || !Entry->TextureRHI.IsValid() || W.PixelData.Num() == 0)
			{
				break;
			}
			FUpdateTextureRegion2D Region(0, 0, 0, 0, W.Width, W.Height);
			RHIUpdateTexture2D(Entry->TextureRHI, 0, Region, W.RowBytes, W.PixelData.GetData());
			break;
		}

		case EResourceWorkKind::DestroyTexture:
		{
			// Ultralight-initiated destroy: no-op for external entries (their
			// lifetime is managed by UnregisterExternalTexture).
			if (const FGPUTextureEntry* Entry = Textures.Find(W.Id))
			{
				if (Entry->bIsExternal)
				{
					break;
				}
			}
			Textures.Remove(W.Id);
			FScopeLock Lock(&ExposedTexLock);
			ExposedTextures.Remove(W.Id);
			break;
		}

		case EResourceWorkKind::ExternalUnregister:
		{
			// Unconditional removal — ExposedTextures was already cleared on
			// the game thread by UnregisterExternalTexture.
			Textures.Remove(W.Id);
			break;
		}

		case EResourceWorkKind::CreateRenderBuffer:
		{
			FGPURenderBufferEntry Entry;
			Entry.TextureId = W.BoundTextureId;
			Entry.Width  = W.Width;
			Entry.Height = W.Height;
			RenderBuffers.Add(W.Id, MoveTemp(Entry));
			break;
		}

		case EResourceWorkKind::DestroyRenderBuffer:
		{
			RenderBuffers.Remove(W.Id);
			break;
		}

		case EResourceWorkKind::CreateGeometry:
		case EResourceWorkKind::UpdateGeometry:
		{
			// UpdateGeometry: drop the previous entry so the new buffers replace
			// it (matches the original "always recreate" comment in the prior
			// implementation; the skeleton's draw cost is small).
			if (W.Kind == EResourceWorkKind::UpdateGeometry)
			{
				Geometries.Remove(W.Id);
			}

			FGPUGeometryEntry Entry;
			Entry.Format = W.VertexFormat;
			Entry.VertexBytes = static_cast<uint32>(W.VertexData.Num());
			Entry.IndexBytes  = static_cast<uint32>(W.IndexData.Num());

			if (Entry.VertexBytes > 0)
			{
				const FRHIBufferCreateDesc Desc =
					FRHIBufferCreateDesc::CreateVertex(TEXT("TSICWebVB"), Entry.VertexBytes)
					.AddUsage(EBufferUsageFlags::Dynamic)
					.DetermineInitialState();
				Entry.VertexBuffer = RHICmdList.CreateBuffer(Desc);
				if (Entry.VertexBuffer.IsValid())
				{
					void* Mapped = RHICmdList.LockBuffer(Entry.VertexBuffer, 0, Entry.VertexBytes, RLM_WriteOnly);
					FMemory::Memcpy(Mapped, W.VertexData.GetData(), Entry.VertexBytes);
					RHICmdList.UnlockBuffer(Entry.VertexBuffer);
				}
			}
			if (Entry.IndexBytes > 0)
			{
				const FRHIBufferCreateDesc Desc =
					FRHIBufferCreateDesc::CreateIndex<uint32>(TEXT("TSICWebIB"), Entry.IndexBytes / sizeof(uint32))
					.AddUsage(EBufferUsageFlags::Dynamic)
					.DetermineInitialState();
				Entry.IndexBuffer = RHICmdList.CreateBuffer(Desc);
				if (Entry.IndexBuffer.IsValid())
				{
					void* Mapped = RHICmdList.LockBuffer(Entry.IndexBuffer, 0, Entry.IndexBytes, RLM_WriteOnly);
					FMemory::Memcpy(Mapped, W.IndexData.GetData(), Entry.IndexBytes);
					RHICmdList.UnlockBuffer(Entry.IndexBuffer);
				}
			}
			Geometries.Add(W.Id, MoveTemp(Entry));
			break;
		}

		case EResourceWorkKind::DestroyGeometry:
		{
			Geometries.Remove(W.Id);
			break;
		}
		}
	}
}

void FWebGPUDriver::ExecuteCommands()
{
	TArray<FResourceWork>       Work;
	TArray<ultralight::Command> Commands;
	{
		FScopeLock Lock(&WorkLock);
		Work = MoveTemp(PendingWork);
		PendingWork.Reset();
	}
	{
		FScopeLock Lock(&CommandLock);
		Commands = MoveTemp(PendingCommands);
		PendingCommands.Reset();
	}
	if (Work.Num() == 0 && Commands.Num() == 0)
	{
		return;
	}

	static int32 BatchCount = 0;
	if (++BatchCount == 1 || BatchCount == 60 || BatchCount == 300)
	{
		UE_LOG(LogTSICWebUI, Log, TEXT("[gpu] ExecuteCommands batch #%d work=%d draws=%d"),
			BatchCount, Work.Num(), Commands.Num());
	}

	// Pointers to the render-thread-owned maps. The driver instance outlives
	// any queued render command (we FlushRenderingCommands on destruction).
	TMap<uint32, FGPUTextureEntry>*       TexMap = &Textures;
	TMap<uint32, FGPURenderBufferEntry>*  RBMap  = &RenderBuffers;
	TMap<uint32, FGPUGeometryEntry>*      GeoMap = &Geometries;

	ENQUEUE_RENDER_COMMAND(TSICWebGPUExecute)(
		[this, Work = MoveTemp(Work), Commands = MoveTemp(Commands), TexMap, RBMap, GeoMap]
		(FRHICommandListImmediate& RHICmdList) mutable
		{
			ApplyResourceWork(RHICmdList, Work);

			// The render thread's immediate command list is already in the
			// Graphics pipeline; calling SwitchPipeline(Graphics) here would
			// double-begin the active breadcrumb (UE 5.6 RHI assertion).
			for (const ultralight::Command& Cmd : Commands)
			{
				const uint32 RBID = Cmd.gpu_state.render_buffer_id;
				FGPURenderBufferEntry* RBEntry = RBMap->Find(RBID);
				if (!RBEntry) continue;
				FGPUTextureEntry* RTTexEntry = TexMap->Find(RBEntry->TextureId);
				if (!RTTexEntry || !RTTexEntry->TextureRHI.IsValid()) continue;
				FTextureRHIRef RTTexture = RTTexEntry->TextureRHI;

				if (Cmd.command_type == ultralight::CommandType::ClearRenderBuffer)
				{
					RHICmdList.Transition(FRHITransitionInfo(RTTexture, ERHIAccess::Unknown, ERHIAccess::RTV));
					FRHIRenderPassInfo RPInfo(RTTexture, ERenderTargetActions::Clear_Store);
					RHICmdList.BeginRenderPass(RPInfo, TEXT("TSICWebClear"));
					RHICmdList.EndRenderPass();
					continue;
				}

				if (Cmd.command_type != ultralight::CommandType::DrawGeometry)
				{
					continue;
				}

				FGPUGeometryEntry* Geo = GeoMap->Find(Cmd.geometry_id);
				if (!Geo || !Geo->VertexBuffer.IsValid() || !Geo->IndexBuffer.IsValid()) continue;

				const bool bIsPath = (Cmd.gpu_state.shader_type == ultralight::ShaderType::FillPath);

				RHICmdList.Transition(FRHITransitionInfo(RTTexture, ERHIAccess::Unknown, ERHIAccess::RTV));
				FRHIRenderPassInfo RPInfo(RTTexture, ERenderTargetActions::Load_Store);
				RHICmdList.BeginRenderPass(RPInfo, TEXT("TSICWebDraw"));

				const float VW = static_cast<float>(Cmd.gpu_state.viewport_width);
				const float VH = static_cast<float>(Cmd.gpu_state.viewport_height);
				RHICmdList.SetViewport(0.f, 0.f, 0.f, VW, VH, 1.f);

				if (Cmd.gpu_state.enable_scissor)
				{
					RHICmdList.SetScissorRect(true,
						Cmd.gpu_state.scissor_rect.left,
						Cmd.gpu_state.scissor_rect.top,
						Cmd.gpu_state.scissor_rect.right,
						Cmd.gpu_state.scissor_rect.bottom);
				}
				else
				{
					RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
				}

				FGraphicsPipelineStateInitializer PSO;
				RHICmdList.ApplyCachedRenderTargets(PSO);
				PSO.PrimitiveType = PT_TriangleList;
				PSO.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				PSO.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				// Ultralight outputs premultiplied alpha: BlendOp(SrcAlphaOp=ADD, Src=ONE, Dst=INV_SRC_ALPHA)
				PSO.BlendState = Cmd.gpu_state.enable_blend
					? TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI()
					: TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero>::GetRHI();

				FTSICWebFillVS::FParameters Params{};
				FillShaderParams(Params, Cmd.gpu_state, *TexMap);

				if (bIsPath)
				{
					TShaderMapRef<FTSICWebFillPathVS> VS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
					TShaderMapRef<FTSICWebFillPathPS> PS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
					PSO.BoundShaderState.VertexDeclarationRHI = GTSICWebPathVertexDeclaration.VertexDeclarationRHI;
					PSO.BoundShaderState.VertexShaderRHI = VS.GetVertexShader();
					PSO.BoundShaderState.PixelShaderRHI  = PS.GetPixelShader();
					SetGraphicsPipelineState(RHICmdList, PSO, 0);
					SetShaderParameters(RHICmdList, VS, VS.GetVertexShader(), Params);
					SetShaderParameters(RHICmdList, PS, PS.GetPixelShader(), Params);
				}
				else
				{
					TShaderMapRef<FTSICWebFillVS> VS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
					TShaderMapRef<FTSICWebFillPS> PS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
					PSO.BoundShaderState.VertexDeclarationRHI = GTSICWebQuadVertexDeclaration.VertexDeclarationRHI;
					PSO.BoundShaderState.VertexShaderRHI = VS.GetVertexShader();
					PSO.BoundShaderState.PixelShaderRHI  = PS.GetPixelShader();
					SetGraphicsPipelineState(RHICmdList, PSO, 0);
					SetShaderParameters(RHICmdList, VS, VS.GetVertexShader(), Params);
					SetShaderParameters(RHICmdList, PS, PS.GetPixelShader(), Params);
				}

				RHICmdList.SetStreamSource(0, Geo->VertexBuffer, 0);
				const uint32 NumPrimitives = Cmd.indices_count / 3;
				RHICmdList.DrawIndexedPrimitive(
					Geo->IndexBuffer,
					/*BaseVertexIndex*/ 0,
					/*MinIndex*/        0,
					/*NumVertices*/     Geo->VertexBytes / (bIsPath ? 20 : 140),
					/*StartIndex*/      Cmd.indices_offset,
					/*NumPrimitives*/   NumPrimitives,
					/*NumInstances*/    1);

				RHICmdList.EndRenderPass();
			}
		});
}

FTextureRHIRef FWebGPUDriver::GetRHITexture(uint32_t texture_id) const
{
	FScopeLock Lock(&ExposedTexLock);
	const FTextureRHIRef* Found = ExposedTextures.Find(texture_id);
	return Found ? *Found : FTextureRHIRef();
}

} // namespace TSICWebUI
