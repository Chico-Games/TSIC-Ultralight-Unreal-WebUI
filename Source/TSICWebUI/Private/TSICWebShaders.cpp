#include "TSICWebShaders.h"
#include "RHIStaticStates.h"

IMPLEMENT_GLOBAL_SHADER(FTSICWebFillVS,     "/Plugin/TSICWebUI/Private/TSICWebFill.usf",     "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FTSICWebFillPS,     "/Plugin/TSICWebUI/Private/TSICWebFill.usf",     "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FTSICWebFillPathVS, "/Plugin/TSICWebUI/Private/TSICWebFillPath.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FTSICWebFillPathPS, "/Plugin/TSICWebUI/Private/TSICWebFillPath.usf", "MainPS", SF_Pixel);

TGlobalResource<FTSICWebPathVertexDeclaration> GTSICWebPathVertexDeclaration;
TGlobalResource<FTSICWebQuadVertexDeclaration> GTSICWebQuadVertexDeclaration;

void FTSICWebPathVertexDeclaration::InitRHI(FRHICommandListBase& /*RHICmdList*/)
{
	static constexpr uint16 Stride = 20;
	FVertexDeclarationElementList Elements;
	Elements.Add(FVertexElement(0,  0, VET_Float2, 0, Stride));
	// VET_UByte4N treats the 4 bytes as RGBA (matches Ultralight / AppCore D3D11
	// which uses DXGI_FORMAT_R8G8B8A8_UNORM). VET_Color would swap to BGRA.
	Elements.Add(FVertexElement(0,  8, VET_UByte4N, 1, Stride));
	Elements.Add(FVertexElement(0, 12, VET_Float2, 2, Stride));
	VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
}

void FTSICWebPathVertexDeclaration::ReleaseRHI()
{
	VertexDeclarationRHI.SafeRelease();
}

void FTSICWebQuadVertexDeclaration::InitRHI(FRHICommandListBase& /*RHICmdList*/)
{
	static constexpr uint16 Stride = 140;
	FVertexDeclarationElementList Elements;
	Elements.Add(FVertexElement(0,   0, VET_Float2, 0,  Stride));
	// VET_UByte4N: RGBA byte order (matches AppCore's DXGI_FORMAT_R8G8B8A8_UNORM).
	Elements.Add(FVertexElement(0,   8, VET_UByte4N, 1,  Stride));
	Elements.Add(FVertexElement(0,  12, VET_Float2, 2,  Stride));
	Elements.Add(FVertexElement(0,  20, VET_Float2, 3,  Stride));
	Elements.Add(FVertexElement(0,  28, VET_Float4, 4,  Stride));
	Elements.Add(FVertexElement(0,  44, VET_Float4, 5,  Stride));
	Elements.Add(FVertexElement(0,  60, VET_Float4, 6,  Stride));
	Elements.Add(FVertexElement(0,  76, VET_Float4, 7,  Stride));
	Elements.Add(FVertexElement(0,  92, VET_Float4, 8,  Stride));
	Elements.Add(FVertexElement(0, 108, VET_Float4, 9,  Stride));
	Elements.Add(FVertexElement(0, 124, VET_Float4, 10, Stride));
	VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
}

void FTSICWebQuadVertexDeclaration::ReleaseRHI()
{
	VertexDeclarationRHI.SafeRelease();
}
