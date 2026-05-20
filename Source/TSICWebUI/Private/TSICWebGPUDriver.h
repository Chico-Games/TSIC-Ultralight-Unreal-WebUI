#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "RHI.h"
#include "RHIResources.h"

THIRD_PARTY_INCLUDES_START
#include <Ultralight/platform/GPUDriver.h>
THIRD_PARTY_INCLUDES_END

namespace TSICWebUI
{
	struct FGPUTextureEntry
	{
		FTextureRHIRef TextureRHI;
		uint32 Width = 0;
		uint32 Height = 0;
		bool bIsRenderTarget = false;
		bool bIsExternal = false;   // owned by the caller; do not destroy on DestroyTexture.
	};

	struct FGPURenderBufferEntry
	{
		uint32 TextureId = 0;
		uint32 Width = 0;
		uint32 Height = 0;
	};

	struct FGPUGeometryEntry
	{
		FBufferRHIRef VertexBuffer;
		FBufferRHIRef IndexBuffer;
		ultralight::VertexBufferFormat Format;
		uint32 VertexBytes = 0;
		uint32 IndexBytes = 0;
	};

	/**
	 * Minimal Ultralight GPUDriver implementation built on UE's RHI.
	 *
	 * This is a SKELETON: resource lifecycle (create/update/destroy for textures,
	 * render buffers, geometry) is plumbed, but DrawGeometry currently only opens
	 * and closes a render pass without issuing shader-bound draw calls. The
	 * intent is to compile and register without crashing so the actual draw
	 * implementation can be staged on top.
	 *
	 * @see Reference/UltralightUE-original for the upstream skeleton this was
	 *       adapted from.
	 */
	class FWebGPUDriver final : public ultralight::GPUDriver
	{
	public:
		FWebGPUDriver();
		virtual ~FWebGPUDriver() override;

		//~ ultralight::GPUDriver
		virtual void BeginSynchronize() override;
		virtual void EndSynchronize() override;

		virtual uint32_t NextTextureId() override;
		virtual void CreateTexture(uint32_t texture_id, ultralight::RefPtr<ultralight::Bitmap> bitmap) override;
		virtual void UpdateTexture(uint32_t texture_id, ultralight::RefPtr<ultralight::Bitmap> bitmap) override;
		virtual void DestroyTexture(uint32_t texture_id) override;

		virtual uint32_t NextRenderBufferId() override;
		virtual void CreateRenderBuffer(uint32_t render_buffer_id, const ultralight::RenderBuffer& buffer) override;
		virtual void DestroyRenderBuffer(uint32_t render_buffer_id) override;

		virtual uint32_t NextGeometryId() override;
		virtual void CreateGeometry(uint32_t geometry_id, const ultralight::VertexBuffer& vertices, const ultralight::IndexBuffer& indices) override;
		virtual void UpdateGeometry(uint32_t geometry_id, const ultralight::VertexBuffer& vertices, const ultralight::IndexBuffer& indices) override;
		virtual void DestroyGeometry(uint32_t geometry_id) override;

		virtual void UpdateCommandList(const ultralight::CommandList& list) override;
		//~

		/** Dispatch any queued commands. Call after Renderer::Render() on the game thread. */
		void ExecuteCommands();

		/** Look up an RHI texture by Ultralight texture id (used by Slate brush wiring). */
		FTextureRHIRef GetRHITexture(uint32_t texture_id) const;

		/** Register an externally-owned RHI texture under a fresh Ultralight texture ID. Game thread only. */
		uint32 RegisterExternalTexture(FTextureRHIRef RHI, uint32 Width, uint32 Height);

		/** Drop a previously-registered external texture. Game thread only. */
		void UnregisterExternalTexture(uint32 TextureId);

	private:
		uint32 NextTexId = 1;
		uint32 NextRBId = 1;
		uint32 NextGeoId = 1;

		TMap<uint32, FGPUTextureEntry> Textures;
		TMap<uint32, FGPURenderBufferEntry> RenderBuffers;
		TMap<uint32, FGPUGeometryEntry> Geometries;

		TArray<ultralight::Command> PendingCommands;
		FCriticalSection CommandLock;
	};
}
