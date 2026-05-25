#include "TSICWebGPUDriver.h"
#include "TSICWebShaders.h"
#include "TSICWebUI.h"

#include "PipelineStateCache.h"
#include "RHICommandList.h"
#include "RHIDefinitions.h"
#include "RHIResources.h"
#include "RHIResourceUtils.h"
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

// Each driver callback enqueues its own ENQUEUE_RENDER_COMMAND. The render
// thread executes them sequentially in submission order, mirroring how the
// pre-2e5706e43 code worked synchronously on the game thread (which forced an
// implicit FlushRenderingCommands per call) without ever calling the immediate
// command list from the game thread — that was the original AV-in-UnlockBuffer
// crash. The map mutations all happen on the render thread, so the maps
// themselves don't need locking; access is serialised by render-thread
// dispatch order.

void FWebGPUDriver::CreateTexture(uint32_t texture_id, ultralight::RefPtr<ultralight::Bitmap> bitmap)
{
	const uint32 Width = bitmap->width();
	const uint32 Height = bitmap->height();
	const bool bIsRT = bitmap->IsEmpty();
	EPixelFormat PixelFormat = PF_B8G8R8A8;
	if (!bIsRT && bitmap->format() == ultralight::BitmapFormat::A8_UNORM)
	{
		PixelFormat = PF_A8;
	}
	ETextureCreateFlags TexFlags = ETextureCreateFlags::ShaderResource;
	if (bIsRT)
	{
		TexFlags |= ETextureCreateFlags::RenderTargetable;
	}

	TArray<uint8> PixelData;
	uint32 RowBytes = 0;
	if (!bIsRT)
	{
		RowBytes = bitmap->row_bytes();
		PixelData.SetNumUninitialized(RowBytes * Height);
		void* Pixels = bitmap->LockPixels();
		if (Pixels)
		{
			FMemory::Memcpy(PixelData.GetData(), Pixels, RowBytes * Height);
		}
		bitmap->UnlockPixels();
	}

	static int32 LogCount = 0;
	if (++LogCount <= 8 || (LogCount % 50) == 0)
	{
		UE_LOG(LogTSICWebUI, Log, TEXT("[gpu] CreateTexture id=%u %ux%u RT=%d (#%d)"),
			texture_id, Width, Height, bIsRT ? 1 : 0, LogCount);
	}

	FWebGPUDriver* Self = this;
	ENQUEUE_RENDER_COMMAND(TSICWebGPUCreateTexture)(
		[Self, texture_id, Width, Height, RowBytes, PixelFormat, TexFlags, bIsRT,
		 PixelData = MoveTemp(PixelData)]
		(FRHICommandListImmediate& RHICmdList) mutable
		{
			FGPUTextureEntry Entry;
			Entry.Width = Width;
			Entry.Height = Height;
			Entry.bIsRenderTarget = bIsRT;

			FRHITextureCreateDesc Desc =
				FRHITextureCreateDesc::Create2D(TEXT("TSICWebUITexture"), Width, Height, PixelFormat)
				.SetFlags(TexFlags)
				.SetNumMips(1);
			Entry.TextureRHI = RHICmdList.CreateTexture(Desc);
			if (PixelData.Num() > 0 && Entry.TextureRHI.IsValid())
			{
				FUpdateTextureRegion2D Region(0, 0, 0, 0, Width, Height);
				RHIUpdateTexture2D(Entry.TextureRHI, 0, Region, RowBytes, PixelData.GetData());
			}

			if (Entry.TextureRHI.IsValid())
			{
				FScopeLock Lock(&Self->ExposedTexLock);
				Self->ExposedTextures.Add(texture_id, Entry.TextureRHI);
			}
			Self->Textures.Add(texture_id, MoveTemp(Entry));
		});
}

void FWebGPUDriver::UpdateTexture(uint32_t texture_id, ultralight::RefPtr<ultralight::Bitmap> bitmap)
{
	if (bitmap->IsEmpty())
	{
		return;
	}

	const uint32 Width = bitmap->width();
	const uint32 Height = bitmap->height();
	const uint32 RowBytes = bitmap->row_bytes();

	TArray<uint8> PixelData;
	PixelData.SetNumUninitialized(RowBytes * Height);
	void* Pixels = bitmap->LockPixels();
	if (Pixels)
	{
		FMemory::Memcpy(PixelData.GetData(), Pixels, RowBytes * Height);
	}
	bitmap->UnlockPixels();

	FWebGPUDriver* Self = this;
	ENQUEUE_RENDER_COMMAND(TSICWebGPUUpdateTexture)(
		[Self, texture_id, Width, Height, RowBytes, PixelData = MoveTemp(PixelData)]
		(FRHICommandListImmediate& /*RHICmdList*/)
		{
			FGPUTextureEntry* Entry = Self->Textures.Find(texture_id);
			if (!Entry || !Entry->TextureRHI.IsValid())
			{
				return;
			}
			FUpdateTextureRegion2D Region(0, 0, 0, 0, Width, Height);
			RHIUpdateTexture2D(Entry->TextureRHI, 0, Region, RowBytes, PixelData.GetData());
		});
}

void FWebGPUDriver::DestroyTexture(uint32_t texture_id)
{
	static int32 DestroyCount = 0;
	if (++DestroyCount <= 8 || (DestroyCount % 50) == 0)
	{
		UE_LOG(LogTSICWebUI, Log, TEXT("[gpu] DestroyTexture id=%u (#%d)"), texture_id, DestroyCount);
	}

	FWebGPUDriver* Self = this;
	ENQUEUE_RENDER_COMMAND(TSICWebGPUDestroyTexture)(
		[Self, texture_id](FRHICommandListImmediate&)
		{
			// External entries are removed only via UnregisterExternalTexture —
			// Ultralight may call DestroyTexture on them which we ignore here.
			if (const FGPUTextureEntry* Entry = Self->Textures.Find(texture_id))
			{
				if (Entry->bIsExternal)
				{
					return;
				}
			}
			Self->Textures.Remove(texture_id);
			{
				FScopeLock Lock(&Self->ExposedTexLock);
				Self->ExposedTextures.Remove(texture_id);
			}
		});
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
	// look the id up via GetRHITexture before the next render-thread tick
	// (e.g., Slate brush wiring) see the texture.
	{
		FScopeLock Lock(&ExposedTexLock);
		ExposedTextures.Add(NewId, RHI);
	}

	// Insert into the render-thread Textures map (used by the draw loop's
	// shader-parameter binding).
	FWebGPUDriver* Self = this;
	ENQUEUE_RENDER_COMMAND(TSICWebGPURegisterExternal)(
		[Self, NewId, RHI, Width, Height](FRHICommandListImmediate&)
		{
			FGPUTextureEntry Entry;
			Entry.TextureRHI = RHI;
			Entry.Width = Width;
			Entry.Height = Height;
			Entry.bIsRenderTarget = false;
			Entry.bIsExternal = true;
			Self->Textures.Add(NewId, MoveTemp(Entry));
		});
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

	FWebGPUDriver* Self = this;
	ENQUEUE_RENDER_COMMAND(TSICWebGPUUnregisterExternal)(
		[Self, TextureId](FRHICommandListImmediate&)
		{
			Self->Textures.Remove(TextureId);
		});
	UE_LOG(LogTSICWebUI, Log, TEXT("[gpu] UnregisterExternalTexture id=%u"), TextureId);
}

void FWebGPUDriver::CreateRenderBuffer(uint32_t render_buffer_id, const ultralight::RenderBuffer& buffer)
{
	const uint32 BoundTextureId = buffer.texture_id;
	const uint32 Width = buffer.width;
	const uint32 Height = buffer.height;
	FWebGPUDriver* Self = this;
	ENQUEUE_RENDER_COMMAND(TSICWebGPUCreateRB)(
		[Self, render_buffer_id, BoundTextureId, Width, Height](FRHICommandListImmediate&)
		{
			FGPURenderBufferEntry Entry;
			Entry.TextureId = BoundTextureId;
			Entry.Width = Width;
			Entry.Height = Height;
			Self->RenderBuffers.Add(render_buffer_id, MoveTemp(Entry));
		});
}

void FWebGPUDriver::DestroyRenderBuffer(uint32_t render_buffer_id)
{
	FWebGPUDriver* Self = this;
	ENQUEUE_RENDER_COMMAND(TSICWebGPUDestroyRB)(
		[Self, render_buffer_id](FRHICommandListImmediate&)
		{
			Self->RenderBuffers.Remove(render_buffer_id);
		});
}

namespace
{
	// Shared body for CreateGeometry / UpdateGeometry: pure render-thread RHI
	// buffer allocation with initial data via SetInitActionResourceArray, no
	// Lock/Unlock pattern.
	FGPUGeometryEntry BuildGeometryEntry(
		FRHICommandListImmediate& RHICmdList,
		ultralight::VertexBufferFormat Format,
		const TArray<uint8>& VertexData,
		const TArray<uint8>& IndexData)
	{
		FGPUGeometryEntry Entry;
		Entry.Format = Format;
		Entry.VertexBytes = static_cast<uint32>(VertexData.Num());
		Entry.IndexBytes = static_cast<uint32>(IndexData.Num());

		if (Entry.VertexBytes > 0)
		{
			FResourceArrayUploadArrayView UploadView(VertexData.GetData(), Entry.VertexBytes);
			const FRHIBufferCreateDesc Desc =
				FRHIBufferCreateDesc::CreateVertex(TEXT("TSICWebVB"), Entry.VertexBytes)
				.AddUsage(EBufferUsageFlags::Static)
				.SetInitActionResourceArray(&UploadView)
				.DetermineInitialState();
			Entry.VertexBuffer = RHICmdList.CreateBuffer(Desc);
		}
		if (Entry.IndexBytes > 0)
		{
			FResourceArrayUploadArrayView UploadView(IndexData.GetData(), Entry.IndexBytes);
			const FRHIBufferCreateDesc Desc =
				FRHIBufferCreateDesc::CreateIndex<uint32>(TEXT("TSICWebIB"), Entry.IndexBytes / sizeof(uint32))
				.AddUsage(EBufferUsageFlags::Static)
				.SetInitActionResourceArray(&UploadView)
				.DetermineInitialState();
			Entry.IndexBuffer = RHICmdList.CreateBuffer(Desc);
		}
		return Entry;
	}
}

void FWebGPUDriver::CreateGeometry(uint32_t geometry_id,
	const ultralight::VertexBuffer& vertices, const ultralight::IndexBuffer& indices)
{
	const ultralight::VertexBufferFormat Format = vertices.format;
	TArray<uint8> VertexData;
	VertexData.Append(static_cast<const uint8*>(vertices.data), vertices.size);
	TArray<uint8> IndexData;
	IndexData.Append(static_cast<const uint8*>(indices.data), indices.size);

	FWebGPUDriver* Self = this;
	ENQUEUE_RENDER_COMMAND(TSICWebGPUCreateGeometry)(
		[Self, geometry_id, Format, VertexData = MoveTemp(VertexData), IndexData = MoveTemp(IndexData)]
		(FRHICommandListImmediate& RHICmdList) mutable
		{
			FGPUGeometryEntry Entry = BuildGeometryEntry(RHICmdList, Format, VertexData, IndexData);
			Self->Geometries.Add(geometry_id, MoveTemp(Entry));
		});
}

void FWebGPUDriver::UpdateGeometry(uint32_t geometry_id,
	const ultralight::VertexBuffer& vertices, const ultralight::IndexBuffer& indices)
{
	const ultralight::VertexBufferFormat Format = vertices.format;
	TArray<uint8> VertexData;
	VertexData.Append(static_cast<const uint8*>(vertices.data), vertices.size);
	TArray<uint8> IndexData;
	IndexData.Append(static_cast<const uint8*>(indices.data), indices.size);

	FWebGPUDriver* Self = this;
	ENQUEUE_RENDER_COMMAND(TSICWebGPUUpdateGeometry)(
		[Self, geometry_id, Format, VertexData = MoveTemp(VertexData), IndexData = MoveTemp(IndexData)]
		(FRHICommandListImmediate& RHICmdList) mutable
		{
			// Drop the prior entry so the new buffers fully replace it.
			Self->Geometries.Remove(geometry_id);
			FGPUGeometryEntry Entry = BuildGeometryEntry(RHICmdList, Format, VertexData, IndexData);
			Self->Geometries.Add(geometry_id, MoveTemp(Entry));
		});
}

void FWebGPUDriver::DestroyGeometry(uint32_t geometry_id)
{
	FWebGPUDriver* Self = this;
	ENQUEUE_RENDER_COMMAND(TSICWebGPUDestroyGeometry)(
		[Self, geometry_id](FRHICommandListImmediate&)
		{
			Self->Geometries.Remove(geometry_id);
		});
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
		auto BindTexture = [&Textures, DefaultTex](uint32 TexId, int32 Slot) -> FRHITexture*
		{
			if (TexId == 0) return DefaultTex;
			const TSICWebUI::FGPUTextureEntry* Entry = Textures.Find(TexId);
			if (!Entry || !Entry->TextureRHI.IsValid())
			{
				// Log fallback-to-black for non-trivial texture IDs (external
				// textures live in NextTexId-allocated space which starts at 1
				// and increments per call; pure-Ultralight ids are also unique
				// so a missing id is always a bug worth surfacing).
				static FCriticalSection LogLock;
				static TSet<uint32> Logged;
				FScopeLock Lock(&LogLock);
				if (!Logged.Contains(TexId))
				{
					Logged.Add(TexId);
					UE_LOG(LogTSICWebUI, Warning,
						TEXT("[gpu] BindTexture slot %d: tex id=%u NOT FOUND in render-thread map (falling back to GBlackTexture). entry-exists=%d rhi-valid=%d"),
						Slot, TexId,
						Entry ? 1 : 0,
						(Entry && Entry->TextureRHI.IsValid()) ? 1 : 0);
				}
				return DefaultTex;
			}
			return Entry->TextureRHI.GetReference();
		};
		Params.Texture0 = BindTexture(State.texture_1_id, 0);
		Params.Texture1 = BindTexture(State.texture_2_id, 1);
		Params.Sampler0 = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}
}

void FWebGPUDriver::ExecuteCommands()
{
	TArray<ultralight::Command> Commands;
	{
		FScopeLock Lock(&CommandLock);
		Commands = MoveTemp(PendingCommands);
		PendingCommands.Reset();
	}
	if (Commands.Num() == 0)
	{
		return;
	}

	static int32 BatchCount = 0;
	++BatchCount;
	if (BatchCount == 1 || BatchCount == 60 || BatchCount == 300 || (BatchCount % 300) == 0)
	{
		UE_LOG(LogTSICWebUI, Log, TEXT("[gpu] ExecuteCommands batch #%d draws=%d textures=%d RBs=%d geos=%d"),
			BatchCount, Commands.Num(), Textures.Num(), RenderBuffers.Num(), Geometries.Num());
	}

	// Pointers to the render-thread-owned maps. Every prior driver callback
	// in this frame enqueued its own render command that mutated these maps,
	// so by the time THIS render command runs (after all those), the maps
	// reflect all pending creates/updates/destroys.
	TMap<uint32, FGPUTextureEntry>*       TexMap = &Textures;
	TMap<uint32, FGPURenderBufferEntry>*  RBMap  = &RenderBuffers;
	TMap<uint32, FGPUGeometryEntry>*      GeoMap = &Geometries;

	ENQUEUE_RENDER_COMMAND(TSICWebGPUExecute)(
		[this, Commands = MoveTemp(Commands), TexMap, RBMap, GeoMap]
		(FRHICommandListImmediate& RHICmdList) mutable
		{
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

				// Batch all transitions for this draw into one call. Any sampled
				// texture that's also a render target (path atlas, glyph atlas)
				// may have been bound as RTV in a previous draw in this batch —
				// move it to SRVGraphics so the shader can sample it. Mirrors the
				// AppCore D3D12 reference's per-texture is_bound_render_target
				// pattern (BindTexture issues RT→SRV barrier when transitioning
				// from RTV state). UE's RHI accepts ERHIAccess::Unknown as the
				// source state and figures out the actual prior state, so we
				// don't have to track it explicitly per-texture.
				TArray<FRHITransitionInfo, TInlineAllocator<3>> DrawTransitions;
				auto MaybeAddSrvTransition = [&DrawTransitions, TexMap, &RTTexture](uint32 TexId)
				{
					if (TexId == 0) return;
					const FGPUTextureEntry* Entry = TexMap->Find(TexId);
					if (!Entry || !Entry->TextureRHI.IsValid()) return;
					// Skip the texture that's about to be bound as the RT for this
					// draw — it gets the RTV transition below, not SRV.
					if (Entry->TextureRHI.GetReference() == RTTexture.GetReference()) return;
					if (!Entry->bIsRenderTarget) return;
					DrawTransitions.Emplace(Entry->TextureRHI, ERHIAccess::Unknown, ERHIAccess::SRVGraphics);
				};
				MaybeAddSrvTransition(Cmd.gpu_state.texture_1_id);
				MaybeAddSrvTransition(Cmd.gpu_state.texture_2_id);
				DrawTransitions.Emplace(RTTexture, ERHIAccess::Unknown, ERHIAccess::RTV);
				RHICmdList.Transition(MakeArrayView(DrawTransitions.GetData(), DrawTransitions.Num()));

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
