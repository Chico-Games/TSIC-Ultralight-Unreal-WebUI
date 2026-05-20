// Copyright Chico Games, Inc. All Rights Reserved.
#include "TSICWebTexProvider.h"
#include "TSICWebTexRegistry.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"
#include "UObject/SoftObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogTSICWebTex, Log, All);

namespace
{
    constexpr int32 kIconCacheMax = 512;
    constexpr double kRenderTargetCacheSeconds = 0.033; // ~30 Hz

    struct FIconCacheEntry { TArray<uint8> PngBytes; double LastAccess = 0.0; };
    TMap<FString, FIconCacheEntry> GIconCache;

    struct FRtCacheEntry { TArray<uint8> PngBytes; double Expires = 0.0; };
    TMap<FString, FRtCacheEntry> GRtCache;

    FCriticalSection GTexCacheLock;

    bool EncodeRawBgraToPng(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<uint8>& OutPng)
    {
        if (Pixels.Num() != Width * Height) return false;

        IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        const TSharedPtr<IImageWrapper> Wrapper = WrapperModule.CreateImageWrapper(EImageFormat::PNG);
        if (!Wrapper.IsValid()) return false;

        if (!Wrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
            return false;

        const TArray64<uint8>& Encoded = Wrapper->GetCompressed(85);
        OutPng = TArray<uint8>(Encoded.GetData(), Encoded.Num());
        return true;
    }

    bool EncodeTexture2DToPng(UTexture2D* Texture, TArray<uint8>& OutPng)
    {
        if (!IsValid(Texture) || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
            return false;

        FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
        const int32 Width = Mip.SizeX;
        const int32 Height = Mip.SizeY;
        const int32 NumPixels = Width * Height;
        if (NumPixels <= 0) return false;

        const FColor* Raw = static_cast<const FColor*>(Mip.BulkData.LockReadOnly());
        if (!Raw) return false;

        TArray<FColor> Pixels;
        Pixels.SetNumUninitialized(NumPixels);
        FMemory::Memcpy(Pixels.GetData(), Raw, NumPixels * sizeof(FColor));
        Mip.BulkData.Unlock();

        return EncodeRawBgraToPng(Pixels, Width, Height, OutPng);
    }

    bool EncodeRenderTargetToPng(UTextureRenderTarget2D* RT, TArray<uint8>& OutPng)
    {
        if (!IsValid(RT)) return false;
        FTextureRenderTargetResource* Resource = RT->GameThread_GetRenderTargetResource();
        if (!Resource) return false;

        TArray<FColor> Pixels;
        const FIntRect Rect(0, 0, RT->SizeX, RT->SizeY);
        FReadSurfaceDataFlags Flags(RCM_UNorm);
        Flags.SetLinearToGamma(false);
        if (!Resource->ReadPixels(Pixels, Flags, Rect)) return false;

        return EncodeRawBgraToPng(Pixels, RT->SizeX, RT->SizeY, OutPng);
    }

    /** Walk the asset registry once + lazy-cache name → UTexture2D.
     *  Avoids hard-linking the gameplay module by reflecting on the
     *  "Thumbnail" TSoftObjectPtr<UTexture2D> property of any class that has it. */
    UTexture2D* FindItemIconTexture(const FString& InItemId)
    {
        static TMap<FString, TWeakObjectPtr<UTexture2D>> ResolvedIconCache;

        if (const TWeakObjectPtr<UTexture2D>* Found = ResolvedIconCache.Find(InItemId))
        {
            if (UTexture2D* Tex = Found->Get())
            {
                return Tex;
            }
            ResolvedIconCache.Remove(InItemId);
        }

        IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

        // Scan registry for any asset whose AssetName matches InItemId.
        FARFilter Filter;
        Filter.bRecursiveClasses = true;
        Filter.bIncludeOnlyOnDiskAssets = false;
        TArray<FAssetData> AllAssets;
        Registry.GetAssets(Filter, AllAssets);

        const FName SearchName(*InItemId);
        for (const FAssetData& Asset : AllAssets)
        {
            if (Asset.AssetName != SearchName)
            {
                continue;
            }
            UObject* Obj = Asset.GetAsset();
            if (!IsValid(Obj))
            {
                continue;
            }
            FProperty* Prop = Obj->GetClass()->FindPropertyByName(FName("Thumbnail"));
            if (!Prop) continue;
            FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Prop);
            if (!SoftProp) continue;
            const FSoftObjectPtr* SoftPtr = SoftProp->ContainerPtrToValuePtr<FSoftObjectPtr>(Obj);
            if (!SoftPtr) continue;
            UObject* Loaded = SoftPtr->LoadSynchronous();
            if (UTexture2D* Tex = Cast<UTexture2D>(Loaded))
            {
                ResolvedIconCache.Add(InItemId, Tex);
                return Tex;
            }
        }
        return nullptr;
    }

    void EvictIfFull()
    {
        if (GIconCache.Num() < kIconCacheMax) return;
        TArray<TPair<FString, double>> Sorted;
        Sorted.Reserve(GIconCache.Num());
        for (const auto& Pair : GIconCache) Sorted.Add({Pair.Key, Pair.Value.LastAccess});
        Sorted.Sort([](const TPair<FString,double>& A, const TPair<FString,double>& B){ return A.Value < B.Value; });
        for (int32 i = 0; i < 64 && i < Sorted.Num(); ++i) GIconCache.Remove(Sorted[i].Key);
    }
}

bool TSICWebTex::IsTexUrl(const FString& Url)
{
    return Url.StartsWith(TEXT("tex://"));
}

FString TSICWebTex::GetTexMimeType()
{
    return TEXT("image/png");
}

void TSICWebTex::ResetCache()
{
    FScopeLock Lock(&GTexCacheLock);
    GIconCache.Empty();
    GRtCache.Empty();
}

bool TSICWebTex::ResolveAndEncode(const FString& Url, UGameInstance* GameInstance, TArray<uint8>& OutBytes)
{
    if (!IsTexUrl(Url)) return false;

    FString WithoutScheme = Url.RightChop(6); // strip "tex://"
    // Strip query string for cache-key purposes; query is used by JS for cache-busting only.
    FString PathPart, QueryPart;
    if (!WithoutScheme.Split(TEXT("?"), &PathPart, &QueryPart))
    {
        PathPart = WithoutScheme;
    }

    // Item icons: tex://item-icon/<ItemId>
    static const FString IconPrefix = TEXT("item-icon/");
    if (PathPart.StartsWith(IconPrefix))
    {
        const FString ItemId = PathPart.RightChop(IconPrefix.Len());
        {
            FScopeLock Lock(&GTexCacheLock);
            if (FIconCacheEntry* Hit = GIconCache.Find(ItemId))
            {
                Hit->LastAccess = FPlatformTime::Seconds();
                OutBytes = Hit->PngBytes;
                return true;
            }
        }
        UTexture2D* Tex = FindItemIconTexture(ItemId);
        TArray<uint8> Png;
        if (!Tex || !EncodeTexture2DToPng(Tex, Png))
        {
            UE_LOG(LogTSICWebTex, Verbose, TEXT("tex://item-icon/%s: no icon resolved"), *ItemId);
            return false;
        }
        {
            FScopeLock Lock(&GTexCacheLock);
            EvictIfFull();
            FIconCacheEntry Entry;
            Entry.PngBytes = Png;
            Entry.LastAccess = FPlatformTime::Seconds();
            GIconCache.Add(ItemId, MoveTemp(Entry));
        }
        OutBytes = Png;
        return true;
    }

    // Render-target keys: tex://<key>
    {
        FScopeLock Lock(&GTexCacheLock);
        if (FRtCacheEntry* Hit = GRtCache.Find(PathPart))
        {
            if (FPlatformTime::Seconds() < Hit->Expires)
            {
                OutBytes = Hit->PngBytes;
                return true;
            }
            GRtCache.Remove(PathPart);
        }
    }
    UTSICWebTexRegistry* Registry = GameInstance ? GameInstance->GetSubsystem<UTSICWebTexRegistry>() : nullptr;
    UTextureRenderTarget2D* RT = Registry ? Registry->GetRenderTarget(FName(*PathPart)) : nullptr;
    if (!RT) return false;
    TArray<uint8> Png;
    if (!EncodeRenderTargetToPng(RT, Png)) return false;
    {
        FScopeLock Lock(&GTexCacheLock);
        FRtCacheEntry Entry;
        Entry.PngBytes = Png;
        Entry.Expires = FPlatformTime::Seconds() + kRenderTargetCacheSeconds;
        GRtCache.Add(PathPart, MoveTemp(Entry));
    }
    OutBytes = Png;
    return true;
}
