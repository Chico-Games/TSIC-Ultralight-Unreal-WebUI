#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GenericPlatform/ICursor.h"
#include "InputCoreTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TSICWebEventBus.h"
#include "TSICWebUITypes.h"
#include "Tickable.h"
#include "TSICWebUISubsystem.generated.h"

struct FTSICWebMessageBridgeInfo;
struct FOnAttributeChangeData;
struct FGameplayAttribute;
class UAbilitySystemComponent;

class UTexture2D;
struct FTSICWebViewEntry;

/** Cache entry retained per-tag so the SPA gets the latest payload on (re)connect. */
struct FTSICCachedMessage
{
	FString   PayloadJson;
	FDateTime CachedAt;
};

UCLASS()
class TSICWEBUI_API UTSICWebUISubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bIsInitialized; }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return true; }

	UFUNCTION(BlueprintCallable, Category = "TSIC Web UI")
	FName CreateView(FName ViewName, const FTSICWebViewConfig& Config);

	UFUNCTION(BlueprintCallable, Category = "TSIC Web UI")
	void DestroyView(FName ViewName);

	// Resize an existing view + its backing texture/render-target to match a
	// new pixel resolution. Called from the Slate widget when its allotted
	// geometry changes so the page renders 1:1 with the on-screen size.
	void ResizeView(FName ViewName, int32 NewWidth, int32 NewHeight);

	UFUNCTION(BlueprintCallable, Category = "TSIC Web UI")
	void LoadURL(FName ViewName, const FString& URL);

	UFUNCTION(BlueprintCallable, Category = "TSIC Web UI")
	void LoadHTML(FName ViewName, const FString& Html);

	UFUNCTION(BlueprintCallable, Category = "TSIC Web UI")
	UTexture2D* GetViewTexture(FName ViewName) const;

	bool IsReady() const { return bIsInitialized && RendererPtr != nullptr; }

	// --- Web Gamepad API forwarding (used by UScpUIInputBridgeSubsystem) ---

	/** Fire a connect/disconnect event to all views. */
	void FireGamepadConnectionEvent(uint32 SlotIndex, bool bConnected,
		uint32 AxisCount, uint32 ButtonCount);

	/** Fire an axis change. */
	void FireGamepadAxis(uint32 SlotIndex, uint32 AxisIndex, double Value);

	/** Fire a button change. */
	void FireGamepadButton(uint32 SlotIndex, uint32 ButtonIndex, double Value);

	// --- Input forwarding (called by Slate widget) ---

	void FireMouseMove(FName ViewName, int32 LocalX, int32 LocalY);
	void FireMouseButton(FName ViewName, int32 LocalX, int32 LocalY, FKey Button, bool bIsDown);
	void FireMouseWheel(FName ViewName, int32 DeltaX, int32 DeltaY);
	void FireKeyEvent(FName ViewName, const FKeyEvent& KeyEvent, bool bIsDown);
	void FireCharEvent(FName ViewName, const FCharacterEvent& CharEvent);

	void FocusView(FName ViewName);
	void UnfocusView(FName ViewName);
	bool ViewHasFocus(FName ViewName) const;

	bool IsPointInteractive(FName ViewName, int32 LocalX, int32 LocalY) const;
	EMouseCursor::Type GetCursorForView(FName ViewName) const;

	void SetFocusMode(FName ViewName, EWebViewFocusMode Mode);
	EWebViewFocusMode GetFocusMode(FName ViewName) const;
	void SetFocusReleaseKey(FName ViewName, FKey Key);
	FKey GetFocusReleaseKey(FName ViewName) const;

	// Slate widget registration. The Slate widget hosting a view calls this on Construct
	// so SetFocusCapture() can route Slate user focus to it (without this, only Ultralight
	// view focus would be set and OS key events would not reach the widget).
	void RegisterSlateWidget(FName ViewName, TWeakPtr<class SWidget> Widget);
	void UnregisterSlateWidget(FName ViewName);

	// --- BP-callable focus / interactive-rect API ---

	UFUNCTION(BlueprintCallable, Category = "TSIC Web UI|Input")
	void SetFocusCapture(FName ViewName, bool bCapture);

	UFUNCTION(BlueprintCallable, Category = "TSIC Web UI|Input")
	void SetInteractiveRects(FName ViewName, const TArray<FTSICWebInteractiveRect>& Rects);

	// --- Event Bus ---

	FTSICWebEventBus& GetEventBus() { return *EventBus; }
	const FTSICWebEventBus& GetEventBus() const { return *EventBus; }

	UFUNCTION(BlueprintCallable, Category = "TSIC Web UI|Events")
	void RegisterChannel(FName ChannelName, EWebChannelKind Kind, const FString& Description);

	UFUNCTION(BlueprintCallable, Category = "TSIC Web UI|Events")
	void BroadcastEvent(FName ChannelName, const FString& PayloadJson);

	// Called from JS bindings (game thread).
	void HandleJSSend(FName ChannelName, const FString& PayloadJson, FName SourceView);
	void HandleJSRequest(FName ChannelName, const FString& PayloadJson, int32 RequestId, FName SourceView);
	FString GetEventBusChannelsJson() const;
	void SetInteractiveRectsFromJson(FName ViewName, const FString& RectsJson);

	// Notified by LoadListener when a view's JS context is freshly bound.
	void NotifyJSContextReady(FName ViewName);

	// Notified by LoadListener when the view's current page finishes loading.
	// Recreates the view's UTexture2D so the Slate brush rebinds to a clean
	// surface — without this, the slate widget paints stale (pre-transition)
	// frames until the user manually nudges the window.
	void RecreateViewTextureForLoad(FName ViewName);

	// --- Message / attribute bridges ---

	// Registers a bridge entry; called by TSICWebBridgeMessage<T>().
	void RegisterMessageBridge(FTSICWebMessageBridgeInfo&& Info);

	// Bridge an ASC attribute onto a sticky bus channel. The provided callback
	// runs every time the attribute changes and returns the JSON payload to broadcast.
	// Re-registering with the same channel replaces the previous binding.
	void BridgeAttribute(FName Channel, UAbilitySystemComponent* ASC,
		const FGameplayAttribute& PrimaryAttribute, const FGameplayAttribute& OptionalMaxAttribute);

	// Unregister a previously-bridged attribute (e.g. when the player pawn dies).
	void UnbridgeAttribute(FName Channel);

	// --- Ultralight ImageSource bridge (Map screen world-map / FOW) ---

	/**
	 * Register an externally-owned UTexture2D under the given Ultralight ImageSource
	 * identifier so HTML <img src="/runtime/<id>.imgsrc"> composites the texture in-place.
	 * Re-registering with the same identifier unregisters the previous binding.
	 * Game thread only.
	 */
	void RegisterImageSourceFromTexture(FName Identifier, UTexture2D* Tex);

	/** Notify Ultralight that the bound texture's pixels changed. */
	void InvalidateImageSource(FName Identifier);

	/** Remove the binding entirely. */
	void UnregisterImageSource(FName Identifier);

	// Cache-aware bridged broadcast. Called by TSICWebBridgeMessage<T> only.
	// Writes the JSON payload to the per-tag cache (skipped if bTransient) and
	// then delegates to BroadcastEvent for the actual bus dispatch.
	void BroadcastBridgedMessage(FGameplayTag Tag, FName Channel, const FString& PayloadJson, bool bTransient);

	// Returns the age of the cached payload for the given tag, or FTimespan::MaxValue() if absent.
	FTimespan GetCachedAge(FGameplayTag Tag) const;

	void DumpCacheToLog() const;

	// JSON returned by tsic.describeMessages().
	FString GetMessageBridgeDescriptionJson() const;

	void DumpMessageBridgesToLog() const;

	// Called from the JS binding. Looks up the registered bridge for the tag and
	// publishes a gameplay message reconstructed from the JSON payload.
	// Returns true on success.
	bool PublishGameplayMessageFromJson(FName TagName, const FString& PayloadJson);

private:
	void SetupPlatform();
	void TeardownPlatform();
	void UpdateCpuTexture(FTSICWebViewEntry& Entry);
	void UpdateGpuTexture(FTSICWebViewEntry& Entry);
	FTSICWebViewEntry* FindEntry(FName ViewName) const;

	// Allocate a fresh UTexture2D for the entry at the requested size and swap
	// it in (rooting the new one, unrooting the old). Slate's SImage compares
	// brush pointers / underlying textures to decide if a repaint is needed, so
	// reusing the same UTexture2D across a page change makes Slate cache the
	// pre-transition frame even though Ultralight wrote new pixels into it.
	// Recreate the texture whenever we kick a navigation so Slate gets a brand-
	// new pointer to bind to.
	void RecreateViewTexture(FTSICWebViewEntry& Entry);

	void DispatchPendingOutbound();
	void RegisterCoreChannels();
	void RegisterConsoleCommands();
	void UnregisterConsoleCommands();

	TMap<FName, TSharedPtr<FTSICWebViewEntry>> Views;

	void* RendererPtr = nullptr;
	void* FileSystemPtr = nullptr;
	void* FontLoaderPtr = nullptr;
	void* LoggerPtr = nullptr;
	// Optional GPU driver — owned when r.TSICWebUI.GPU=1 at SetupPlatform time.
	void* GPUDriverPtr = nullptr;
	bool bGPUAccelerated = false;

	TUniquePtr<FTSICWebEventBus> EventBus;

	IConsoleCommand* DumpChannelsCmd = nullptr;
	IConsoleCommand* PingCmd = nullptr;
	IConsoleCommand* EvalJSCmd = nullptr;
	IConsoleCommand* DumpMessagesCmd = nullptr;
	IConsoleCommand* DumpCacheCmd = nullptr;

	TMap<FGameplayTag, TSharedPtr<FTSICWebMessageBridgeInfo>> MessageBridges;

	// Per-tag latest JSON payload + write timestamp. No UObject references retained.
	TMap<FGameplayTag, FTSICCachedMessage> CachedPayloadJson;

	struct FAttributeBridgeEntry
	{
		TWeakObjectPtr<UAbilitySystemComponent> ASC;
		FName Channel;
		FName PrimaryName;
		FName MaxName;
		FDelegateHandle PrimaryHandle;
		FDelegateHandle MaxHandle;
	};
	TMap<FName, FAttributeBridgeEntry> AttributeBridges;

	void BroadcastAttributeValue(const FAttributeBridgeEntry& Entry);
	void OnBridgedAttributeChanged(const FOnAttributeChangeData& ChangeData, FName Channel);

	struct FImageSourceEntry
	{
		uint32 TextureId = 0;
		TWeakObjectPtr<UTexture2D> Tex;
		/** Refcounted Ultralight ImageSource — stored as raw void* to keep header light; cpp casts. */
		void* ImageSource = nullptr;
	};
	TMap<FName, FImageSourceEntry> ImageSourceBridges;

	bool bIsInitialized = false;
};
