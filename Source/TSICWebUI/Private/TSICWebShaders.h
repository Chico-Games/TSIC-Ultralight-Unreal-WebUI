#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "RenderResource.h"
#include "RHIDefinitions.h"
#include "ShaderParameterStruct.h"

// Names below must match the globals declared at the top of TSICWebCommon.ush
// (UE's shader reflection links by name).
class FTSICWebFillVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTSICWebFillVS);
	SHADER_USE_PARAMETER_STRUCT(FTSICWebFillVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector4f, State)
		SHADER_PARAMETER(FMatrix44f, Transform)
		SHADER_PARAMETER_ARRAY(FIntVector4, Integer4, [2])
		SHADER_PARAMETER_ARRAY(FVector4f, Scalar4, [2])
		SHADER_PARAMETER_ARRAY(FVector4f, Vector, [8])
		SHADER_PARAMETER(FIntVector4, ClipData)
		SHADER_PARAMETER_ARRAY(FMatrix44f, Clip, [8])
		SHADER_PARAMETER_TEXTURE(Texture2D, Texture0)
		SHADER_PARAMETER_TEXTURE(Texture2D, Texture1)
		SHADER_PARAMETER_SAMPLER(SamplerState, Sampler0)
	END_SHADER_PARAMETER_STRUCT()
};

class FTSICWebFillPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTSICWebFillPS);
	SHADER_USE_PARAMETER_STRUCT(FTSICWebFillPS, FGlobalShader);
	using FParameters = FTSICWebFillVS::FParameters;
};

class FTSICWebFillPathVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTSICWebFillPathVS);
	SHADER_USE_PARAMETER_STRUCT(FTSICWebFillPathVS, FGlobalShader);
	// Path shader needs Transform + clip uniforms (clip applies in PS).
	using FParameters = FTSICWebFillVS::FParameters;
};

class FTSICWebFillPathPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTSICWebFillPathPS);
	SHADER_USE_PARAMETER_STRUCT(FTSICWebFillPathPS, FGlobalShader);
	using FParameters = FTSICWebFillVS::FParameters;
};

class FTSICWebPathVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override;
};

class FTSICWebQuadVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override;
};

extern TGlobalResource<FTSICWebPathVertexDeclaration> GTSICWebPathVertexDeclaration;
extern TGlobalResource<FTSICWebQuadVertexDeclaration> GTSICWebQuadVertexDeclaration;
