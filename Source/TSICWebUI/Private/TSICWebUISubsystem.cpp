#include "TSICWebUISubsystem.h"
#include "TSICWebFileSystem.h"
#include "TSICWebFontLoader.h"
#include "TSICWebGPUDriver.h"
#include "TSICWebJSBindings.h"
#include "TSICWebKeyMappings.h"
#include "TSICWebLoadListener.h"
#include "TSICWebLogger.h"
#include "TSICWebMessageBridge.h"
#include "TSICWebStringHelpers.h"
#include "TSICWebUI.h"
#include "TSICWebViewEntry.h"
#include "TSICWebViewListener.h"

#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "GameplayEffectTypes.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Input/Events.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TextureResource.h"

THIRD_PARTY_INCLUDES_START
#include <Ultralight/Ultralight.h>
#include <Ultralight/Renderer.h>
#include <Ultralight/View.h>
#include <Ultralight/Bitmap.h>
#include <Ultralight/Geometry.h>
#include <Ultralight/ImageSource.h>
#include <Ultralight/String.h>
#include <Ultralight/GamepadEvent.h>
#include <Ultralight/platform/Platform.h>
#include <Ultralight/platform/Config.h>
#include <Ultralight/platform/Surface.h>
THIRD_PARTY_INCLUDES_END

namespace
{
	static TAutoConsoleVariable<int32> CVarTSICWebUIGPU(
		TEXT("r.TSICWebUI.GPU"),
		1, // smoke-testing the full fill.hlsl port
		TEXT("Enable Ultralight GPU rendering path."),
		ECVF_ReadOnly);

	ultralight::RefPtr<ultralight::Renderer>& AsRenderer(void* Ptr)
	{
		return *static_cast<ultralight::RefPtr<ultralight::Renderer>*>(Ptr);
	}

	ultralight::MouseEvent::Button MapMouseButton(const FKey& Key)
	{
		if (Key == EKeys::LeftMouseButton) return ultralight::MouseEvent::kButton_Left;
		if (Key == EKeys::RightMouseButton) return ultralight::MouseEvent::kButton_Right;
		if (Key == EKeys::MiddleMouseButton) return ultralight::MouseEvent::kButton_Middle;
		return ultralight::MouseEvent::kButton_None;
	}
}

void UTSICWebUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (IsRunningCommandlet() || !FApp::CanEverRender())
	{
		UE_LOG(LogTSICWebUI, Log, TEXT("TSICWebUISubsystem: skipping init (commandlet or non-render env)."));
		return;
	}

	const FTSICWebUIModule* Module = FModuleManager::GetModulePtr<FTSICWebUIModule>(TEXT("TSICWebUI"));
	if (!Module || !Module->AreLibrariesLoaded())
	{
		UE_LOG(LogTSICWebUI, Error, TEXT("TSICWebUISubsystem: Ultralight DLLs not loaded; aborting init."));
		return;
	}

	// Platform + Renderer are now module-owned so they survive PIE end/restart.
	// Just ensure they exist; first subsystem to initialise creates them.
	FTSICWebUIModule& WebModule = FTSICWebUIModule::Get();
	WebModule.EnsurePlatformInitialised();
	RendererPtr = WebModule.GetRenderer();
	GPUDriverPtr = WebModule.GetGPUDriver();
	bGPUAccelerated = WebModule.IsGPUAccelerated();
	if (!RendererPtr)
	{
		return;
	}

	EventBus = MakeUnique<FTSICWebEventBus>(this);
	RegisterCoreChannels();
	RegisterConsoleCommands();

	bIsInitialized = true;
}

void UTSICWebUISubsystem::Deinitialize()
{
	// Stop ticking IMMEDIATELY — IsTickable returns false now so the engine
	// won't call Tick after this point. Without this, the subsystem can tick
	// between the destruction of Renderer/Views and the eventual end of
	// Deinitialize, crashing with use-after-free in Renderer->Update.
	bIsInitialized = false;

	UnregisterConsoleCommands();

	// Tear down attribute bridges so we don't leak delegates onto destroyed ASCs.
	{
		TArray<FName> Channels;
		AttributeBridges.GetKeys(Channels);
		for (const FName& Ch : Channels)
		{
			UnbridgeAttribute(Ch);
		}
	}

	// Unregister gameplay-message listeners.
	for (TPair<FGameplayTag, TSharedPtr<FTSICWebMessageBridgeInfo>>& Pair : MessageBridges)
	{
		if (Pair.Value.IsValid() && Pair.Value->ListenerHandle.IsValid())
		{
			Pair.Value->ListenerHandle.Unregister();
		}
	}
	MessageBridges.Reset();
	CachedPayloadJson.Reset();

	// Drop any ImageSource bindings before nulling the GPU driver pointer so
	// UnregisterImageSource can still reach FWebGPUDriver::UnregisterExternalTexture.
	{
		TArray<FName> ImageIds;
		ImageSourceBridges.GetKeys(ImageIds);
		for (FName Id : ImageIds)
		{
			UnregisterImageSource(Id);
		}
	}

	TArray<FName> Names;
	Views.GetKeys(Names);
	for (const FName& Name : Names)
	{
		DestroyView(Name);
	}
	Views.Reset();

	EventBus.Reset();

#if WITH_EDITOR
	// Purge WebCore's in-memory resource cache when PIE ends. The renderer is
	// module-owned and survives PIE end/start, which means WebCore's 64 MiB
	// memory cache (parsed HTML/CSS/JS keyed by URL) survives too — the next
	// PIE session would otherwise serve the previous run's cached resources
	// for the same file:// URLs without ever calling our FileSystem, so HTML
	// edits between PIE runs wouldn't show up until full editor restart.
	if (RendererPtr)
	{
		AsRenderer(RendererPtr)->PurgeMemory();
	}
#endif

	// Don't tear down the platform — it's module-owned and survives PIE
	// restarts. Just clear our local pointers so we don't dangle.
	RendererPtr = nullptr;
	FileSystemPtr = nullptr;
	FontLoaderPtr = nullptr;
	LoggerPtr = nullptr;
	GPUDriverPtr = nullptr;
	bGPUAccelerated = false;

	Super::Deinitialize();
}

void UTSICWebUISubsystem::Tick(float /*DeltaTime*/)
{
	// Guard against ticking during/after Deinitialize. IsTickable already
	// returns false but the engine may have queued one more tick.
	if (!bIsInitialized || !RendererPtr)
	{
		return;
	}

	// One-shot heartbeat so we can confirm Tick is actually running and how many views exist.
	{
		static uint64 TickCount = 0;
		++TickCount;
		if (TickCount == 1 || TickCount == 60 || TickCount == 300)
		{
			UE_LOG(LogTSICWebUI, Log, TEXT("[trace] Subsystem::Tick #%llu views=%d this=%p"),
				TickCount, Views.Num(), this);
		}
	}

	ultralight::RefPtr<ultralight::Renderer>& Renderer = AsRenderer(RendererPtr);
	Renderer->Update();
	// Required for CSS / JS animations (requestAnimationFrame, @keyframes) to tick.
	// See the Ultralight integration docs.
	Renderer->RefreshDisplay(0);

	// Ultralight's needs_paint signal can latch off when content stops changing
	// (or never lights up if the page initialised before the renderer first ticked).
	// Force every view to repaint every frame — the CPU cost is fine for HUD-sized
	// surfaces and we re-upload from dirty_bounds anyway.
	for (auto& Pair : Views)
	{
		FTSICWebViewEntry& Entry = *Pair.Value;
		if (Entry.View.get())
		{
			Entry.View->set_needs_paint(true);
		}
	}

	Renderer->Render();

	// Drain queued GPU commands now (GPU mode only). The driver collects commands
	// during Render() above via UpdateCommandList(); we execute them here so the
	// dispatch happens on the game-thread tick boundary.
	if (bGPUAccelerated && GPUDriverPtr)
	{
		static_cast<TSICWebUI::FWebGPUDriver*>(GPUDriverPtr)->ExecuteCommands();
	}

	for (auto& Pair : Views)
	{
		FTSICWebViewEntry& Entry = *Pair.Value;
		if (!Entry.View.get() || !Entry.Texture.IsValid())
		{
			continue;
		}
		if (bGPUAccelerated)
		{
			UpdateGpuTexture(Entry);
		}
		else
		{
			UpdateCpuTexture(Entry);
		}
	}

	DispatchPendingOutbound();
}

TStatId UTSICWebUISubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTSICWebUISubsystem, STATGROUP_Tickables);
}

FName UTSICWebUISubsystem::CreateView(FName ViewName, const FTSICWebViewConfig& Config)
{
	if (!RendererPtr)
	{
		UE_LOG(LogTSICWebUI, Error, TEXT("CreateView: renderer not initialised."));
		return NAME_None;
	}

	if (Views.Contains(ViewName))
	{
		UE_LOG(LogTSICWebUI, Warning, TEXT("CreateView: view '%s' already exists."), *ViewName.ToString());
		return ViewName;
	}

	const uint32 W = static_cast<uint32>(FMath::Max(1, Config.Width));
	const uint32 H = static_cast<uint32>(FMath::Max(1, Config.Height));

	ultralight::ViewConfig ViewCfg;
	ViewCfg.is_accelerated = bGPUAccelerated;
	ViewCfg.is_transparent = Config.bIsTransparent;
	ViewCfg.initial_device_scale = Config.DeviceScale;
	ViewCfg.enable_javascript = Config.bEnableJavaScript;

	ultralight::RefPtr<ultralight::View> ULView = AsRenderer(RendererPtr)->CreateView(W, H, ViewCfg, nullptr);
	if (!ULView.get())
	{
		UE_LOG(LogTSICWebUI, Error, TEXT("CreateView: Ultralight CreateView returned null for '%s'."), *ViewName.ToString());
		return NAME_None;
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!Texture)
	{
		UE_LOG(LogTSICWebUI, Error, TEXT("CreateView: failed to create UTexture2D for '%s'."), *ViewName.ToString());
		return NAME_None;
	}

	// Match the texture configuration that UImage::SetBrushFromTexture applies and that
	// the project's WorldTextureManager uses for its runtime CPU-uploaded textures:
	//  - NeverStream — required for UpdateTextureRegions to actually upload (Texture2D.cpp:1428)
	//  - bForceMiplevelsToBeResident + bIgnoreStreamingMipBias — UImage applies these so the
	//    GPU keeps the mip data resident, otherwise the brush draws blank.
	//  - LODGroup = Pixels2D — canonical "runtime 2D image" group; default world group has
	//    streaming-aware flags that can suppress UpdateTextureRegions.
	//  - SRGB = false — Ultralight gives us pre-multiplied BGRA8 linear pixels.
	Texture->SRGB = false;
	Texture->Filter = TF_Bilinear;
	Texture->NeverStream = true;
	Texture->LODGroup = TEXTUREGROUP_Pixels2D;
	Texture->bForceMiplevelsToBeResident = true;
	Texture->bIgnoreStreamingMipBias = true;
	Texture->AddToRoot();
	Texture->UpdateResource();

	TSharedPtr<FTSICWebViewEntry> Entry = MakeShared<FTSICWebViewEntry>();
	Entry->ViewName = ViewName;
	Entry->View = ULView;
	Entry->Texture = Texture;
	Entry->Width = W;
	Entry->Height = H;
	Entry->FocusMode = Config.FocusMode;
	Entry->FocusReleaseKey = Config.FocusReleaseKey;

	Entry->ViewListener = MakeUnique<FTSICWebViewListener>(this, ViewName);
	Entry->LoadListener = MakeUnique<FTSICWebLoadListener>(this, ViewName);
	ULView->set_view_listener(Entry->ViewListener.Get());
	ULView->set_load_listener(Entry->LoadListener.Get());

	Views.Add(ViewName, Entry);

	UE_LOG(LogTSICWebUI, Log, TEXT("CreateView '%s' (%ux%u) texture=%p"),
		*ViewName.ToString(), W, H, Texture);

	return ViewName;
}

void UTSICWebUISubsystem::ResizeView(FName ViewName, int32 NewWidth, int32 NewHeight)
{
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	if (!Entry || !Entry->View.get())
	{
		return;
	}

	const uint32 W = static_cast<uint32>(FMath::Max(1, NewWidth));
	const uint32 H = static_cast<uint32>(FMath::Max(1, NewHeight));
	if (Entry->Width == W && Entry->Height == H)
	{
		return;
	}

	// Resize the Ultralight view so the page reflows + re-renders at the new
	// pixel resolution.
	Entry->View->Resize(W, H);
	Entry->Width = W;
	Entry->Height = H;

	RecreateViewTexture(*Entry);

	UE_LOG(LogTSICWebUI, Log, TEXT("ResizeView '%s' -> %ux%u"),
		*ViewName.ToString(), W, H);
}

void UTSICWebUISubsystem::RecreateViewTexture(FTSICWebViewEntry& Entry)
{
	const uint32 W = FMath::Max<uint32>(1, Entry.Width);
	const uint32 H = FMath::Max<uint32>(1, Entry.Height);

	UTexture2D* OldTexture = Entry.Texture.Get();
	UTexture2D* NewTexture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!NewTexture)
	{
		UE_LOG(LogTSICWebUI, Warning, TEXT("RecreateViewTexture('%s'): CreateTransient failed for %ux%u."),
			*Entry.ViewName.ToString(), W, H);
		return;
	}
	// Match the texture configuration that CreateView and ResizeView apply so
	// the new texture behaves identically to the one it replaces.
	NewTexture->SRGB = false;
	NewTexture->Filter = TF_Bilinear;
	NewTexture->NeverStream = true;
	NewTexture->LODGroup = TEXTUREGROUP_Pixels2D;
	NewTexture->bForceMiplevelsToBeResident = true;
	NewTexture->bIgnoreStreamingMipBias = true;
	NewTexture->AddToRoot();
	NewTexture->UpdateResource();
	Entry.Texture = NewTexture;
	if (OldTexture)
	{
		OldTexture->RemoveFromRoot();
	}
}

void UTSICWebUISubsystem::DestroyView(FName ViewName)
{
	TSharedPtr<FTSICWebViewEntry>* Found = Views.Find(ViewName);
	if (!Found)
	{
		return;
	}

	FTSICWebViewEntry& Entry = **Found;
	if (Entry.View.get())
	{
		Entry.View->set_view_listener(nullptr);
		Entry.View->set_load_listener(nullptr);
	}
	Entry.View = nullptr;
	Entry.ViewListener.Reset();
	Entry.LoadListener.Reset();

	if (UTexture2D* Tex = Entry.Texture.Get())
	{
		Tex->RemoveFromRoot();
	}
	Entry.Texture.Reset();

	Views.Remove(ViewName);

	// Forget any JS->C++ binding tokens that reference this view name so that
	// a late callback after destruction can't route into a recycled view's slot.
	TSICWebUI::JSBindings::ReleaseTokensForView(ViewName);

	UE_LOG(LogTSICWebUI, Log, TEXT("DestroyView '%s'"), *ViewName.ToString());
}

void UTSICWebUISubsystem::RecreateViewTextureForLoad(FName ViewName)
{
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	if (!Entry || !Entry->View.get())
	{
		return;
	}

	// Ghost-resize: ask Ultralight to reflow at +1px, then immediately back to
	// the original. Two real-dimensioned Resize calls force the view to
	// re-render layout immediately, which is what the user observes as the
	// "drag the window edge a pixel" workaround. set_needs_paint alone is
	// insufficient — it gets latched on every frame yet Slate stays on the
	// pre-transition brush. Pair with a fresh UTexture2D so the slate widget
	// binds onto a clean surface whose first painted frame is the new page.
	const uint32 W = FMath::Max<uint32>(1, Entry->Width);
	const uint32 H = FMath::Max<uint32>(1, Entry->Height);
	if (Entry->View.get())
	{
		Entry->View->Resize(W + 1, H);
		Entry->View->Resize(W, H);
	}
	RecreateViewTexture(*Entry);
	UE_LOG(LogTSICWebUI, Log, TEXT("RecreateViewTextureForLoad '%s' (%ux%u) — nudged reflow + new texture"),
		*ViewName.ToString(), W, H);
}

void UTSICWebUISubsystem::LoadURL(FName ViewName, const FString& URL)
{
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	if (!Entry || !Entry->View.get())
	{
		return;
	}
	Entry->bJSContextReady = false;
	Entry->View->LoadURL(TSICWebUI::FStringToUL(URL));
	// Swap the Slate-facing UTexture2D for a fresh one. Slate caches paint by
	// brush+texture pointer, and reusing the same UTexture2D across a page
	// change leaves the slate widget painting the pre-transition frame even
	// once Ultralight has rewritten the pixels. Recreating the texture forces
	// SImage::SetImage to invalidate on the next Tick and the page swap shows
	// up without the user having to nudge the window. (A window resize works
	// today only as a side effect of ResizeView calling this same path.)
	RecreateViewTexture(*Entry);
	UE_LOG(LogTSICWebUI, Log, TEXT("LoadURL '%s' -> %s"), *ViewName.ToString(), *URL);
}

void UTSICWebUISubsystem::LoadHTML(FName ViewName, const FString& Html)
{
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	if (!Entry || !Entry->View.get())
	{
		return;
	}
	Entry->bJSContextReady = false;
	Entry->View->LoadHTML(TSICWebUI::FStringToUL(Html));
}

UTexture2D* UTSICWebUISubsystem::GetViewTexture(FName ViewName) const
{
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	return Entry ? Entry->Texture.Get() : nullptr;
}

FTSICWebViewEntry* UTSICWebUISubsystem::FindEntry(FName ViewName) const
{
	const TSharedPtr<FTSICWebViewEntry>* Found = Views.Find(ViewName);
	return (Found && Found->IsValid()) ? Found->Get() : nullptr;
}

// =========================================================================
// Input forwarding
// =========================================================================

void UTSICWebUISubsystem::FireMouseMove(FName ViewName, int32 LocalX, int32 LocalY)
{
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	if (!Entry || !Entry->View.get())
	{
		return;
	}
	ultralight::MouseEvent Evt;
	Evt.type = ultralight::MouseEvent::kType_MouseMoved;
	Evt.x = LocalX;
	Evt.y = LocalY;
	Evt.button = ultralight::MouseEvent::kButton_None;
	Entry->View->FireMouseEvent(Evt);
}

void UTSICWebUISubsystem::FireMouseButton(FName ViewName, int32 LocalX, int32 LocalY, FKey Button, bool bIsDown)
{
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	if (!Entry || !Entry->View.get())
	{
		return;
	}
	ultralight::MouseEvent Evt;
	Evt.type = bIsDown ? ultralight::MouseEvent::kType_MouseDown : ultralight::MouseEvent::kType_MouseUp;
	Evt.x = LocalX;
	Evt.y = LocalY;
	Evt.button = MapMouseButton(Button);
	Entry->View->FireMouseEvent(Evt);
}

void UTSICWebUISubsystem::FireMouseWheel(FName ViewName, int32 DeltaX, int32 DeltaY)
{
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	if (!Entry || !Entry->View.get())
	{
		return;
	}
	ultralight::ScrollEvent Evt;
	Evt.type = ultralight::ScrollEvent::kType_ScrollByPixel;
	Evt.delta_x = DeltaX;
	Evt.delta_y = DeltaY;
	Entry->View->FireScrollEvent(Evt);
}

void UTSICWebUISubsystem::FireKeyEvent(FName ViewName, const FKeyEvent& KeyEvent, bool bIsDown)
{
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	if (!Entry || !Entry->View.get())
	{
		return;
	}

	const FKey& Key = KeyEvent.GetKey();
	const int32 VK = TSICWebUI::SlateKeyToVirtualKeyCode(Key);
	if (VK == 0)
	{
		return;
	}

	ultralight::KeyEvent Evt;
	Evt.type = bIsDown ? ultralight::KeyEvent::kType_RawKeyDown : ultralight::KeyEvent::kType_KeyUp;
	Evt.modifiers = TSICWebUI::ModifiersFromInputEvent(KeyEvent);
	Evt.virtual_key_code = VK;
	Evt.native_key_code = VK;
	Evt.is_keypad = TSICWebUI::IsKeypadKey(Key);
	Evt.is_auto_repeat = KeyEvent.IsRepeat();
	Evt.is_system_key = false;
	ultralight::GetKeyIdentifierFromVirtualKeyCode(VK, Evt.key_identifier);

	Entry->View->FireKeyEvent(Evt);
}

void UTSICWebUISubsystem::FireCharEvent(FName ViewName, const FCharacterEvent& CharEvent)
{
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	if (!Entry || !Entry->View.get())
	{
		return;
	}

	TCHAR C = CharEvent.GetCharacter();
	if (C == 0)
	{
		return;
	}

	// Ultralight expects Enter to be emitted as '\n' rather than '\r'.
	// On Windows, Slate gives us '\r' here; rewrite it.
	if (C == TEXT('\r'))
	{
		C = TEXT('\n');
	}

	// Ultralight's `text` and `unmodified_text` are utf-8 strings — pass the char through.
	FString CharStr;
	CharStr.AppendChar(C);

	ultralight::KeyEvent Evt;
	Evt.type = ultralight::KeyEvent::kType_Char;
	Evt.modifiers = TSICWebUI::ModifiersFromInputEvent(CharEvent);
	Evt.virtual_key_code = 0;
	Evt.native_key_code = 0;
	Evt.text = TSICWebUI::FStringToUL(CharStr);
	Evt.unmodified_text = Evt.text;
	Evt.is_keypad = false;
	Evt.is_auto_repeat = CharEvent.IsRepeat();
	Evt.is_system_key = false;

	Entry->View->FireKeyEvent(Evt);
}

void UTSICWebUISubsystem::FocusView(FName ViewName)
{
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	if (!Entry || !Entry->View.get())
	{
		return;
	}
	if (Entry->bHasFocus)
	{
		return;
	}
	Entry->bHasFocus = true;
	Entry->View->Focus();
}

void UTSICWebUISubsystem::UnfocusView(FName ViewName)
{
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	if (!Entry || !Entry->View.get())
	{
		return;
	}
	if (!Entry->bHasFocus)
	{
		return;
	}
	Entry->bHasFocus = false;
	Entry->View->Unfocus();
}

bool UTSICWebUISubsystem::ViewHasFocus(FName ViewName) const
{
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	return Entry && Entry->bHasFocus;
}

bool UTSICWebUISubsystem::IsPointInteractive(FName ViewName, int32 LocalX, int32 LocalY) const
{
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	return Entry ? Entry->IsPointInteractive(LocalX, LocalY) : false;
}

EMouseCursor::Type UTSICWebUISubsystem::GetCursorForView(FName ViewName) const
{
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	if (!Entry || !Entry->ViewListener)
	{
		return EMouseCursor::Default;
	}
	return Entry->ViewListener->GetCurrentCursor();
}

void UTSICWebUISubsystem::SetFocusMode(FName ViewName, EWebViewFocusMode Mode)
{
	if (FTSICWebViewEntry* Entry = FindEntry(ViewName))
	{
		Entry->FocusMode = Mode;
	}
}

EWebViewFocusMode UTSICWebUISubsystem::GetFocusMode(FName ViewName) const
{
	if (FTSICWebViewEntry* Entry = FindEntry(ViewName))
	{
		return Entry->FocusMode;
	}
	return EWebViewFocusMode::CaptureOnClick;
}

void UTSICWebUISubsystem::SetFocusReleaseKey(FName ViewName, FKey Key)
{
	if (FTSICWebViewEntry* Entry = FindEntry(ViewName))
	{
		Entry->FocusReleaseKey = Key;
	}
}

FKey UTSICWebUISubsystem::GetFocusReleaseKey(FName ViewName) const
{
	if (FTSICWebViewEntry* Entry = FindEntry(ViewName))
	{
		return Entry->FocusReleaseKey;
	}
	return EKeys::Escape;
}

void UTSICWebUISubsystem::SetFocusCapture(FName ViewName, bool bCapture)
{
	if (bCapture)
	{
		FocusView(ViewName);
	}
	else
	{
		UnfocusView(ViewName);
	}

	// Route Slate's user focus to the hosting widget so OS key/char events flow
	// into STSICWebViewSlate::OnKey*. Without this the Ultralight view receives
	// View::Focus() but Slate keeps focus on the game viewport.
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	if (!Entry || !FSlateApplication::IsInitialized())
	{
		return;
	}

	const TSharedPtr<SWidget> Widget = Entry->SlateWidget.Pin();
	if (!Widget.IsValid())
	{
		return;
	}

	FSlateApplication& App = FSlateApplication::Get();
	if (bCapture)
	{
		App.SetUserFocus(0, Widget.ToSharedRef(), EFocusCause::SetDirectly);
	}
	else if (App.GetUserFocusedWidget(0) == Widget)
	{
		App.SetAllUserFocusToGameViewport();
	}
}

void UTSICWebUISubsystem::RegisterSlateWidget(FName ViewName, TWeakPtr<SWidget> Widget)
{
	if (FTSICWebViewEntry* Entry = FindEntry(ViewName))
	{
		Entry->SlateWidget = Widget;
	}
}

void UTSICWebUISubsystem::UnregisterSlateWidget(FName ViewName)
{
	if (FTSICWebViewEntry* Entry = FindEntry(ViewName))
	{
		Entry->SlateWidget.Reset();
	}
}

void UTSICWebUISubsystem::SetInteractiveRects(FName ViewName, const TArray<FTSICWebInteractiveRect>& Rects)
{
	if (FTSICWebViewEntry* Entry = FindEntry(ViewName))
	{
		Entry->InteractiveRects = Rects;
	}
}

// =========================================================================
// Event bus integration
// =========================================================================

void UTSICWebUISubsystem::RegisterChannel(FName ChannelName, EWebChannelKind Kind, const FString& Description)
{
	if (EventBus)
	{
		EventBus->RegisterChannel(ChannelName, Kind, Description);
	}
}

void UTSICWebUISubsystem::BroadcastEvent(FName ChannelName, const FString& PayloadJson)
{
	if (EventBus)
	{
		EventBus->BroadcastEvent(ChannelName, PayloadJson);
	}
}

void UTSICWebUISubsystem::HandleJSSend(FName ChannelName, const FString& PayloadJson, FName SourceView)
{
	if (EventBus)
	{
		EventBus->EnqueueInboundSend(ChannelName, PayloadJson, SourceView);
	}
}

void UTSICWebUISubsystem::HandleJSRequest(FName ChannelName, const FString& PayloadJson, int32 RequestId, FName SourceView)
{
	if (EventBus)
	{
		EventBus->EnqueueInboundRequest(ChannelName, PayloadJson, RequestId, SourceView);
	}
}

FString UTSICWebUISubsystem::GetEventBusChannelsJson() const
{
	return EventBus ? EventBus->GetChannelDescriptionJson() : TEXT("[]");
}

void UTSICWebUISubsystem::SetInteractiveRectsFromJson(FName ViewName, const FString& RectsJson)
{
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	if (!Entry)
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>> JsonArray;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(RectsJson);
	if (!FJsonSerializer::Deserialize(Reader, JsonArray))
	{
		UE_LOG(LogTSICWebUI, Warning, TEXT("[%s] setInteractiveRects: failed to parse JSON: %s"),
			*ViewName.ToString(), *RectsJson);
		return;
	}

	TArray<FTSICWebInteractiveRect> Rects;
	Rects.Reserve(JsonArray.Num());
	for (const TSharedPtr<FJsonValue>& Val : JsonArray)
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (!Val.IsValid() || !Val->TryGetObject(Obj))
		{
			continue;
		}
		FTSICWebInteractiveRect R;
		R.X = (int32)((*Obj)->GetNumberField(TEXT("x")));
		R.Y = (int32)((*Obj)->GetNumberField(TEXT("y")));
		R.Width = (int32)((*Obj)->GetNumberField(TEXT("w")));
		R.Height = (int32)((*Obj)->GetNumberField(TEXT("h")));
		Rects.Add(R);
	}
	Entry->InteractiveRects = MoveTemp(Rects);

	UE_LOG(LogTSICWebUI, Verbose, TEXT("[%s] interactive rects updated (count=%d)"),
		*ViewName.ToString(), Entry->InteractiveRects.Num());
}

void UTSICWebUISubsystem::NotifyJSContextReady(FName ViewName)
{
	if (!EventBus)
	{
		return;
	}
	FTSICWebViewEntry* Entry = FindEntry(ViewName);
	if (!Entry)
	{
		return;
	}
	Entry->bJSContextReady = true;

	// Sticky-channel replay: collect last-values and route to this view only.
	TArray<FTSICWebPendingOutbound> Replays;
	EventBus->CollectStickyReplays(ViewName, Replays);
	for (FTSICWebPendingOutbound& R : Replays)
	{
		// We're inside Renderer::Update (OnWindowObjectReady). View is locked; dispatch immediately.
		if (Entry->View.get())
		{
			TSICWebUI::JSBindings::DispatchToView(Entry->View.get(), R.Channel, R.PayloadJson, R.RequestId, R.ErrorMessage);
		}
	}

	// Replay every cached bridged-message payload to this view so the SPA gets
	// current state immediately after its JS context binds. Live messages are
	// delivered with no meta; replayed messages carry { cachedAt, ageMs }.
	const FDateTime Now = FDateTime::UtcNow();
	for (const TPair<FGameplayTag, FTSICCachedMessage>& Pair : CachedPayloadJson)
	{
		if (!Pair.Key.IsValid() || !Entry->View.get())
		{
			continue;
		}
		const FName Channel = TSICWebUI::MakeMessageChannelName(Pair.Key);
		const FTimespan Age = Now - Pair.Value.CachedAt;
		const FString MetaJson = FString::Printf(
			TEXT("{\"cachedAt\":\"%s\",\"ageMs\":%lld}"),
			*Pair.Value.CachedAt.ToIso8601(),
			static_cast<int64>(Age.GetTotalMilliseconds()));
		TSICWebUI::JSBindings::DispatchToView(Entry->View.get(), Channel, Pair.Value.PayloadJson, /*RequestId*/ -1, /*Error*/ FString(), MetaJson);
	}
}

void UTSICWebUISubsystem::DispatchPendingOutbound()
{
	if (!EventBus)
	{
		return;
	}

	TArray<FTSICWebPendingOutbound> Pending;
	EventBus->DrainPending(Pending);
	if (Pending.Num() == 0)
	{
		return;
	}

	for (const FTSICWebPendingOutbound& Out : Pending)
	{
		if (Out.TargetView.IsNone())
		{
			// Broadcast to all ready views.
			for (auto& Pair : Views)
			{
				FTSICWebViewEntry& Entry = *Pair.Value;
				if (Entry.bJSContextReady && Entry.View.get())
				{
					TSICWebUI::JSBindings::DispatchToView(Entry.View.get(), Out.Channel, Out.PayloadJson, Out.RequestId, Out.ErrorMessage);
				}
			}
		}
		else
		{
			FTSICWebViewEntry* Entry = FindEntry(Out.TargetView);
			if (Entry && Entry->bJSContextReady && Entry->View.get())
			{
				TSICWebUI::JSBindings::DispatchToView(Entry->View.get(), Out.Channel, Out.PayloadJson, Out.RequestId, Out.ErrorMessage);
			}
		}
	}
}

// =========================================================================
// Message / attribute bridges
// =========================================================================

void UTSICWebUISubsystem::RegisterMessageBridge(FTSICWebMessageBridgeInfo&& Info)
{
	const FGameplayTag Tag = Info.SourceTag;
	TSharedPtr<FTSICWebMessageBridgeInfo>& Slot = MessageBridges.FindOrAdd(Tag);
	if (Slot.IsValid() && Slot->ListenerHandle.IsValid())
	{
		// Replacing an existing bridge — release the old listener.
		Slot->ListenerHandle.Unregister();
	}
	Slot = MakeShared<FTSICWebMessageBridgeInfo>(MoveTemp(Info));
	UE_LOG(LogTSICWebUI, Log, TEXT("MessageBridge: %s -> %s (struct=%s, fields=%d)"),
		*Slot->SourceTag.ToString(), *Slot->BusChannel.ToString(),
		*Slot->StructName, Slot->FieldNames.Num());
}

void UTSICWebUISubsystem::BroadcastBridgedMessage(FGameplayTag Tag, FName Channel, const FString& PayloadJson, bool bTransient)
{
	if (!bTransient && Tag.IsValid())
	{
		FTSICCachedMessage& Slot = CachedPayloadJson.FindOrAdd(Tag);
		Slot.PayloadJson = PayloadJson;
		Slot.CachedAt = FDateTime::UtcNow();
	}
	BroadcastEvent(Channel, PayloadJson);
}

FTimespan UTSICWebUISubsystem::GetCachedAge(FGameplayTag Tag) const
{
	if (const FTSICCachedMessage* Entry = CachedPayloadJson.Find(Tag))
	{
		return FDateTime::UtcNow() - Entry->CachedAt;
	}
	return FTimespan::MaxValue();
}

void UTSICWebUISubsystem::DumpCacheToLog() const
{
	UE_LOG(LogTSICWebUI, Display, TEXT("=== TSIC Web UI replay cache (%d) ==="), CachedPayloadJson.Num());
	const FDateTime Now = FDateTime::UtcNow();
	for (const TPair<FGameplayTag, FTSICCachedMessage>& Pair : CachedPayloadJson)
	{
		const FTimespan Age = Now - Pair.Value.CachedAt;
		const FString Preview = Pair.Value.PayloadJson.Left(80);
		UE_LOG(LogTSICWebUI, Display, TEXT("  %s  age=%.2fs  payload=%s%s"),
			*Pair.Key.ToString(),
			Age.GetTotalSeconds(),
			*Preview,
			Pair.Value.PayloadJson.Len() > 80 ? TEXT("…") : TEXT(""));
	}
}

void UTSICWebUISubsystem::BroadcastAttributeValue(const FAttributeBridgeEntry& Entry)
{
	UAbilitySystemComponent* ASC = Entry.ASC.Get();
	if (!ASC || !EventBus)
	{
		return;
	}

	FString Json = TEXT("{\"current\":");
	const FGameplayAttribute Primary = FGameplayAttribute(FindFProperty<FProperty>(ASC->GetClass(), Entry.PrimaryName));
	float CurrentVal = 0.f;
	if (Primary.IsValid())
	{
		CurrentVal = ASC->GetNumericAttribute(Primary);
	}
	Json += FString::SanitizeFloat(CurrentVal);
	if (!Entry.MaxName.IsNone())
	{
		const FGameplayAttribute Max = FGameplayAttribute(FindFProperty<FProperty>(ASC->GetClass(), Entry.MaxName));
		float MaxVal = 1.f;
		if (Max.IsValid())
		{
			MaxVal = ASC->GetNumericAttribute(Max);
		}
		Json += TEXT(",\"max\":");
		Json += FString::SanitizeFloat(MaxVal);
	}
	Json += TEXT("}");

	BroadcastEvent(Entry.Channel, Json);
}

void UTSICWebUISubsystem::OnBridgedAttributeChanged(const FOnAttributeChangeData& /*ChangeData*/, FName Channel)
{
	FAttributeBridgeEntry* Entry = AttributeBridges.Find(Channel);
	if (!Entry)
	{
		return;
	}
	BroadcastAttributeValue(*Entry);
}

void UTSICWebUISubsystem::BridgeAttribute(FName Channel, UAbilitySystemComponent* ASC,
	const FGameplayAttribute& PrimaryAttribute, const FGameplayAttribute& OptionalMaxAttribute)
{
	if (!ASC || !PrimaryAttribute.IsValid())
	{
		return;
	}

	UnbridgeAttribute(Channel);

	if (EventBus)
	{
		EventBus->RegisterChannel(Channel, EWebChannelKind::Sticky,
			FString::Printf(TEXT("Live attribute: %s%s%s"),
				*PrimaryAttribute.GetName(),
				OptionalMaxAttribute.IsValid() ? TEXT(" / ") : TEXT(""),
				OptionalMaxAttribute.IsValid() ? *OptionalMaxAttribute.GetName() : TEXT("")));
	}

	FAttributeBridgeEntry Entry;
	Entry.ASC = ASC;
	Entry.Channel = Channel;
	Entry.PrimaryName = PrimaryAttribute.GetUProperty()->GetFName();
	Entry.MaxName = OptionalMaxAttribute.IsValid() ? OptionalMaxAttribute.GetUProperty()->GetFName() : NAME_None;

	const FName ChannelCopy = Channel;
	Entry.PrimaryHandle = ASC->GetGameplayAttributeValueChangeDelegate(PrimaryAttribute)
		.AddUObject(this, &UTSICWebUISubsystem::OnBridgedAttributeChanged, ChannelCopy);
	if (OptionalMaxAttribute.IsValid())
	{
		Entry.MaxHandle = ASC->GetGameplayAttributeValueChangeDelegate(OptionalMaxAttribute)
			.AddUObject(this, &UTSICWebUISubsystem::OnBridgedAttributeChanged, ChannelCopy);
	}

	AttributeBridges.Add(Channel, Entry);

	// Push the current value immediately so sticky subscribers can populate.
	BroadcastAttributeValue(Entry);

	UE_LOG(LogTSICWebUI, Log, TEXT("AttributeBridge: %s -> %s%s"),
		*Entry.PrimaryName.ToString(), *Channel.ToString(),
		Entry.MaxName.IsNone() ? TEXT("") : *FString::Printf(TEXT(" + %s"), *Entry.MaxName.ToString()));
}

void UTSICWebUISubsystem::UnbridgeAttribute(FName Channel)
{
	FAttributeBridgeEntry* Entry = AttributeBridges.Find(Channel);
	if (!Entry)
	{
		return;
	}
	if (UAbilitySystemComponent* ASC = Entry->ASC.Get())
	{
		if (Entry->PrimaryHandle.IsValid())
		{
			const FGameplayAttribute Primary = FGameplayAttribute(FindFProperty<FProperty>(ASC->GetClass(), Entry->PrimaryName));
			if (Primary.IsValid())
			{
				ASC->GetGameplayAttributeValueChangeDelegate(Primary).Remove(Entry->PrimaryHandle);
			}
		}
		if (Entry->MaxHandle.IsValid() && !Entry->MaxName.IsNone())
		{
			const FGameplayAttribute Max = FGameplayAttribute(FindFProperty<FProperty>(ASC->GetClass(), Entry->MaxName));
			if (Max.IsValid())
			{
				ASC->GetGameplayAttributeValueChangeDelegate(Max).Remove(Entry->MaxHandle);
			}
		}
	}
	AttributeBridges.Remove(Channel);
}

FString UTSICWebUISubsystem::GetMessageBridgeDescriptionJson() const
{
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const TPair<FGameplayTag, TSharedPtr<FTSICWebMessageBridgeInfo>>& Pair : MessageBridges)
	{
		const TSharedPtr<FTSICWebMessageBridgeInfo>& Info = Pair.Value;
		if (!Info.IsValid())
		{
			continue;
		}
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("tag"), Info->SourceTag.ToString());
		Obj->SetStringField(TEXT("channel"), Info->BusChannel.ToString());
		Obj->SetStringField(TEXT("struct"), Info->StructName);
		Obj->SetStringField(TEXT("description"), Info->Description);
		TArray<TSharedPtr<FJsonValue>> Fields;
		for (const FString& F : Info->FieldNames)
		{
			Fields.Add(MakeShared<FJsonValueString>(F));
		}
		Obj->SetArrayField(TEXT("fields"), Fields);
		Items.Add(MakeShared<FJsonValueObject>(Obj));
	}
	FString Out;
	TSharedRef<TJsonWriter<TCHAR>> Writer = TJsonWriterFactory<TCHAR>::Create(&Out);
	FJsonSerializer::Serialize(Items, Writer);
	return Out;
}

bool UTSICWebUISubsystem::PublishGameplayMessageFromJson(FName TagName, const FString& PayloadJson)
{
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TagName, /*bErrorIfNotFound*/ false);
	if (!Tag.IsValid())
	{
		UE_LOG(LogTSICWebUI, Warning, TEXT("publishMessage: unknown gameplay tag '%s'"), *TagName.ToString());
		return false;
	}
	const TSharedPtr<FTSICWebMessageBridgeInfo>* Found = MessageBridges.Find(Tag);
	if (!Found || !Found->IsValid() || !(*Found)->BroadcastFromJson)
	{
		UE_LOG(LogTSICWebUI, Warning, TEXT("publishMessage: tag '%s' is not bridged (call TSICWebBridgeMessage<T> on the C++ side first)"),
			*TagName.ToString());
		return false;
	}

	const bool bOk = (*Found)->BroadcastFromJson(Tag, PayloadJson, this);
	if (!bOk)
	{
		UE_LOG(LogTSICWebUI, Warning, TEXT("publishMessage: failed to deserialize payload for '%s' (struct=%s): %s"),
			*TagName.ToString(), *(*Found)->StructName, *PayloadJson);
	}
	return bOk;
}

void UTSICWebUISubsystem::DumpMessageBridgesToLog() const
{
	UE_LOG(LogTSICWebUI, Display, TEXT("=== TSIC Web UI message bridges (%d) ==="), MessageBridges.Num());
	for (const TPair<FGameplayTag, TSharedPtr<FTSICWebMessageBridgeInfo>>& Pair : MessageBridges)
	{
		const TSharedPtr<FTSICWebMessageBridgeInfo>& Info = Pair.Value;
		if (!Info.IsValid())
		{
			continue;
		}
		UE_LOG(LogTSICWebUI, Display, TEXT("  %s -> %s  struct=%s  fields=[%s]  desc=%s"),
			*Info->SourceTag.ToString(),
			*Info->BusChannel.ToString(),
			*Info->StructName,
			*FString::Join(Info->FieldNames, TEXT(",")),
			*Info->Description);
	}

	UE_LOG(LogTSICWebUI, Display, TEXT("=== TSIC Web UI attribute bridges (%d) ==="), AttributeBridges.Num());
	for (const TPair<FName, FAttributeBridgeEntry>& Pair : AttributeBridges)
	{
		const FAttributeBridgeEntry& E = Pair.Value;
		UE_LOG(LogTSICWebUI, Display, TEXT("  %s = %s%s  asc=%s"),
			*E.Channel.ToString(),
			*E.PrimaryName.ToString(),
			E.MaxName.IsNone() ? TEXT("") : *FString::Printf(TEXT(" / %s"), *E.MaxName.ToString()),
			E.ASC.IsValid() ? *GetNameSafe(E.ASC->GetOwner()) : TEXT("(stale)"));
	}
}

void UTSICWebUISubsystem::RegisterCoreChannels()
{
	if (!EventBus)
	{
		return;
	}

	// Event example: ping/pong for smoke tests.
	EventBus->RegisterChannel(FName(TEXT("tsic.test.ping")), EWebChannelKind::Event,
		TEXT("Smoke-test fire-and-forget. Payload is echoed to tsic.test.pong."));

	EventBus->RegisterChannel(FName(TEXT("tsic.test.pong")), EWebChannelKind::Event,
		TEXT("Smoke-test response sent in reaction to tsic.test.ping."));

	// Request example: returns a fake inventory list.
	EventBus->RegisterChannel(FName(TEXT("tsic.test.echo")), EWebChannelKind::Event,
		TEXT("Request channel. Echoes its payload back as the response."));

	// Wire the test handlers.
	EventBus->RegisterHandler(FName(TEXT("tsic.test.ping")),
		FTSICWebEventHandler::CreateLambda([this](FName /*Channel*/, const FString& Payload)
		{
			UE_LOG(LogTSICWebUI, Display, TEXT("[bus] tsic.test.ping payload=%s"), *Payload);
			BroadcastEvent(FName(TEXT("tsic.test.pong")), Payload);
		}));

	EventBus->RegisterRequestHandler(FName(TEXT("tsic.test.echo")),
		FTSICWebRequestHandler::CreateLambda([](FName /*Channel*/, const FString& Payload) -> TOptional<FString>
		{
			return Payload; // round-trip echo
		}));
}

void UTSICWebUISubsystem::RegisterConsoleCommands()
{
	IConsoleManager& Mgr = IConsoleManager::Get();
	DumpChannelsCmd = Mgr.RegisterConsoleCommand(
		TEXT("WebUI.DumpChannels"),
		TEXT("Print all registered TSIC Web UI event-bus channels to the log."),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			if (EventBus)
			{
				EventBus->DumpToLog();
			}
		}),
		ECVF_Default);

	PingCmd = Mgr.RegisterConsoleCommand(
		TEXT("WebUI.Ping"),
		TEXT("Fire a tsic.test.pong broadcast from C++ for verification."),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			if (EventBus)
			{
				BroadcastEvent(FName(TEXT("tsic.test.pong")), TEXT("\"hello-from-cpp\""));
			}
		}),
		ECVF_Default);

	DumpMessagesCmd = Mgr.RegisterConsoleCommand(
		TEXT("WebUI.DumpMessages"),
		TEXT("Print all bridged gameplay-message tags (and attribute bridges) to the log."),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			DumpMessageBridgesToLog();
		}),
		ECVF_Default);

	DumpCacheCmd = Mgr.RegisterConsoleCommand(
		TEXT("WebUI.DumpCache"),
		TEXT("Print the per-tag replay cache (tag, age, payload preview) to the log."),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			DumpCacheToLog();
		}),
		ECVF_Default);

	PurgeCacheCmd = Mgr.RegisterConsoleCommand(
		TEXT("WebUI.PurgeCache"),
		TEXT("Purge WebCore's in-memory resource cache so the next page load re-reads HTML/CSS/JS from disk. ")
		TEXT("Use after editing HTML mid-PIE; combine with a reload to see the change immediately."),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			if (!RendererPtr)
			{
				UE_LOG(LogTSICWebUI, Warning, TEXT("WebUI.PurgeCache: renderer not initialised."));
				return;
			}
			AsRenderer(RendererPtr)->PurgeMemory();
			UE_LOG(LogTSICWebUI, Display, TEXT("WebUI.PurgeCache: WebCore memory cache purged."));
		}),
		ECVF_Default);

	EvalJSCmd = Mgr.RegisterConsoleCommand(
		TEXT("WebUI.EvalJS"),
		TEXT("WebUI.EvalJS <viewname> <script> -- run a script on a view's JS context."),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogTSICWebUI, Warning, TEXT("WebUI.EvalJS <viewname> <script...>"));
				return;
			}
			const FName ViewName(*Args[0]);
			FString Script;
			for (int32 i = 1; i < Args.Num(); ++i)
			{
				if (i > 1) Script.AppendChar(TEXT(' '));
				Script.Append(Args[i]);
			}
			FTSICWebViewEntry* Entry = FindEntry(ViewName);
			if (!Entry || !Entry->View.get())
			{
				UE_LOG(LogTSICWebUI, Warning, TEXT("WebUI.EvalJS: no view '%s'"), *ViewName.ToString());
				return;
			}
			ultralight::String Exception;
			ultralight::String Result = Entry->View->EvaluateScript(TSICWebUI::FStringToUL(Script), &Exception);
			UE_LOG(LogTSICWebUI, Display, TEXT("WebUI.EvalJS: %s => %s%s"),
				*Script,
				*TSICWebUI::ULToFString(Result),
				Exception.empty() ? TEXT("") : *FString::Printf(TEXT("  (exception: %s)"), *TSICWebUI::ULToFString(Exception)));
		}),
		ECVF_Default);
}

void UTSICWebUISubsystem::UnregisterConsoleCommands()
{
	if (DumpChannelsCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(DumpChannelsCmd);
		DumpChannelsCmd = nullptr;
	}
	if (PingCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(PingCmd);
		PingCmd = nullptr;
	}
	if (EvalJSCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(EvalJSCmd);
		EvalJSCmd = nullptr;
	}
	if (DumpMessagesCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(DumpMessagesCmd);
		DumpMessagesCmd = nullptr;
	}
	if (DumpCacheCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(DumpCacheCmd);
		DumpCacheCmd = nullptr;
	}
	if (PurgeCacheCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(PurgeCacheCmd);
		PurgeCacheCmd = nullptr;
	}
}

// =========================================================================
// Platform setup / texture upload (unchanged from MVP, slight reformat)
// =========================================================================

void UTSICWebUISubsystem::SetupPlatform()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("TSICWebUI"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogTSICWebUI, Error, TEXT("SetupPlatform: plugin manifest not found."));
		return;
	}

	const FString BaseDir = Plugin->GetBaseDir();
	const FString ResourceRoot = FPaths::Combine(BaseDir, TEXT("Source/ThirdParty/UltralightSDK/Binaries/Win64"));
	const FString ContentRoot = FPaths::Combine(BaseDir, TEXT("Content/UI/Web"));
	const FString CacheDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UltralightCache"));

	IFileManager::Get().MakeDirectory(*CacheDir, /*Tree*/ true);

	ultralight::Config Cfg;
	Cfg.cache_path = TSICWebUI::FStringToUL(CacheDir);
	Cfg.resource_path_prefix = "resources/";
	Cfg.face_winding = ultralight::FaceWinding::Clockwise;

	FTSICWebLogger* Logger = new FTSICWebLogger();
	FTSICWebFileSystem* FileSystem = new FTSICWebFileSystem(ContentRoot, ResourceRoot);
	FTSICWebFontLoader* FontLoader = new FTSICWebFontLoader();

	LoggerPtr = Logger;
	FileSystemPtr = FileSystem;
	FontLoaderPtr = FontLoader;

	// Plumb the GameInstance through so tex:// URLs can reach the registry.
	FileSystem->SetGameInstance(GetGameInstance());

	ultralight::Platform& Platform = ultralight::Platform::instance();
	Platform.set_config(Cfg);
	Platform.set_logger(Logger);
	Platform.set_file_system(FileSystem);
	Platform.set_font_loader(FontLoader);

	bGPUAccelerated = CVarTSICWebUIGPU.GetValueOnGameThread() != 0;
	if (bGPUAccelerated)
	{
		TSICWebUI::FWebGPUDriver* GPUDriver = new TSICWebUI::FWebGPUDriver();
		GPUDriverPtr = GPUDriver;
		Platform.set_gpu_driver(GPUDriver);
		UE_LOG(LogTSICWebUI, Log, TEXT("SetupPlatform: GPU driver registered (r.TSICWebUI.GPU=1)"));
	}

	ultralight::RefPtr<ultralight::Renderer> Renderer = ultralight::Renderer::Create();
	if (!Renderer.get())
	{
		UE_LOG(LogTSICWebUI, Error, TEXT("SetupPlatform: Renderer::Create() returned null."));
		TeardownPlatform();
		return;
	}

	RendererPtr = new ultralight::RefPtr<ultralight::Renderer>(Renderer);
	UE_LOG(LogTSICWebUI, Log, TEXT("SetupPlatform: Ultralight renderer created (cache=%s)"), *CacheDir);
}

void UTSICWebUISubsystem::TeardownPlatform()
{
	// Make sure any pending RHI work that references our resources completes
	// before we tear down. Without this, the render thread can still be
	// touching textures/views that we're about to delete.
	FlushRenderingCommands();

	// Disconnect Ultralight from our pointers FIRST so its async worker threads
	// can't reach into freed memory while the Renderer destructor unwinds.
	ultralight::Platform& Platform = ultralight::Platform::instance();
	Platform.set_gpu_driver(nullptr);
	Platform.set_font_loader(nullptr);
	Platform.set_file_system(nullptr);
	Platform.set_logger(nullptr);

	if (RendererPtr)
	{
		delete static_cast<ultralight::RefPtr<ultralight::Renderer>*>(RendererPtr);
		RendererPtr = nullptr;
	}

	// Flush again so any render commands queued from the Renderer's destructor
	// have drained before we delete the GPU driver they reference.
	FlushRenderingCommands();

	if (GPUDriverPtr)
	{
		delete static_cast<TSICWebUI::FWebGPUDriver*>(GPUDriverPtr);
		GPUDriverPtr = nullptr;
	}
	bGPUAccelerated = false;

	if (FontLoaderPtr)
	{
		delete static_cast<FTSICWebFontLoader*>(FontLoaderPtr);
		FontLoaderPtr = nullptr;
	}
	if (FileSystemPtr)
	{
		delete static_cast<FTSICWebFileSystem*>(FileSystemPtr);
		FileSystemPtr = nullptr;
	}
	if (LoggerPtr)
	{
		delete static_cast<FTSICWebLogger*>(LoggerPtr);
		LoggerPtr = nullptr;
	}
}

void UTSICWebUISubsystem::UpdateCpuTexture(FTSICWebViewEntry& Entry)
{
	UTexture2D* Texture = Entry.Texture.Get();
	if (!Texture || !Entry.View.get())
	{
		static bool bLoggedNoTex = false;
		if (!bLoggedNoTex)
		{
			UE_LOG(LogTSICWebUI, Warning, TEXT("[trace][%s] UpdateCpuTexture: no texture or view"),
				*Entry.ViewName.ToString());
			bLoggedNoTex = true;
		}
		return;
	}

	ultralight::Surface* SurfaceRaw = Entry.View->surface();
	if (!SurfaceRaw)
	{
		static bool bLoggedNoSurface = false;
		if (!bLoggedNoSurface)
		{
			UE_LOG(LogTSICWebUI, Warning, TEXT("[trace][%s] surface() returned null"), *Entry.ViewName.ToString());
			bLoggedNoSurface = true;
		}
		return;
	}

	ultralight::BitmapSurface* Surface = static_cast<ultralight::BitmapSurface*>(SurfaceRaw);
	const ultralight::IntRect Dirty = Surface->dirty_bounds();
	if (Dirty.IsEmpty())
	{
		// Log first time we see an empty dirty bounds (means renderer ran but never produced output).
		static int32 EmptyTickCount = 0;
		if (++EmptyTickCount == 60 || EmptyTickCount == 300)
		{
			UE_LOG(LogTSICWebUI, Log, TEXT("[trace][%s] dirty bounds empty for %d ticks (page may not have loaded)"),
				*Entry.ViewName.ToString(), EmptyTickCount);
		}
		return;
	}

	ultralight::RefPtr<ultralight::Bitmap> Bitmap = Surface->bitmap();
	if (!Bitmap.get())
	{
		UE_LOG(LogTSICWebUI, Warning, TEXT("[trace][%s] Surface->bitmap() null"), *Entry.ViewName.ToString());
		return;
	}

	void* SrcPixels = Bitmap->LockPixels();
	if (!SrcPixels)
	{
		Bitmap->UnlockPixels();
		return;
	}

	const uint32 SrcRowBytes = Bitmap->row_bytes();
	const uint32 SrcWidth = Bitmap->width();
	const uint32 SrcHeight = Bitmap->height();
	const uint32 BytesPerPixel = Bitmap->bpp();
	const uint32 DstRowBytes = SrcWidth * BytesPerPixel;

	// Snapshot pixel data into an owned buffer so we can release Ultralight's bitmap
	// before the render thread runs the upload.
	uint8* Buffer = static_cast<uint8*>(FMemory::Malloc(DstRowBytes * SrcHeight));
	for (uint32 Row = 0; Row < SrcHeight; ++Row)
	{
		FMemory::Memcpy(Buffer + (Row * DstRowBytes),
			static_cast<const uint8*>(SrcPixels) + (Row * SrcRowBytes),
			DstRowBytes);
	}

	// One-shot diagnostic: log the first pixel + a non-zero pixel count from the
	// buffer so we know there's actual content being uploaded.
	{
		static bool bLoggedFirstPixels = false;
		if (!bLoggedFirstPixels)
		{
			bLoggedFirstPixels = true;
			uint32 NonZero = 0;
			const uint32 Count = DstRowBytes * SrcHeight;
			for (uint32 i = 0; i < Count; i += 4)
			{
				if (Buffer[i] != 0 || Buffer[i+1] != 0 || Buffer[i+2] != 0 || Buffer[i+3] != 0)
				{
					NonZero++;
				}
			}
			const uint32 CenterIdx = (SrcHeight / 2) * DstRowBytes + (SrcWidth / 2) * 4;
			UE_LOG(LogTSICWebUI, Display,
				TEXT("[%s] first upload: %ux%u (%u bytes), non-zero pixels=%u, center BGRA=[%u,%u,%u,%u]"),
				*Entry.ViewName.ToString(), SrcWidth, SrcHeight, Count, NonZero,
				Buffer[CenterIdx], Buffer[CenterIdx+1], Buffer[CenterIdx+2], Buffer[CenterIdx+3]);
		}
	}

	Bitmap->UnlockPixels();
	Surface->ClearDirtyBounds();

	// UpdateTextureRegions is the canonical API for incremental UTexture2D updates;
	// it marshals to the render thread internally and reuses the same RHI resource
	// rather than recreating it every frame.
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, SrcWidth, SrcHeight);
	Texture->UpdateTextureRegions(
		/*MipIndex*/ 0,
		/*NumRegions*/ 1,
		Region,
		DstRowBytes,
		BytesPerPixel,
		Buffer,
		[](uint8* InBuffer, const FUpdateTextureRegion2D* InRegion)
		{
			FMemory::Free(InBuffer);
			delete InRegion;
		});
}

void UTSICWebUISubsystem::UpdateGpuTexture(FTSICWebViewEntry& Entry)
{
	UTexture2D* Texture = Entry.Texture.Get();
	if (!Texture || !Entry.View.get() || !GPUDriverPtr)
	{
		return;
	}

	const ultralight::RenderTarget RT = Entry.View->render_target();
	if (RT.is_empty || RT.texture_id == 0)
	{
		static bool bLoggedEmpty = false;
		if (!bLoggedEmpty)
		{
			bLoggedEmpty = true;
			UE_LOG(LogTSICWebUI, Warning, TEXT("[gpu][%s] render_target is empty (tex_id=%u)"),
				*Entry.ViewName.ToString(), RT.texture_id);
		}
		return;
	}

	TSICWebUI::FWebGPUDriver* Driver = static_cast<TSICWebUI::FWebGPUDriver*>(GPUDriverPtr);
	FTextureRHIRef SrcRHI = Driver->GetRHITexture(RT.texture_id);
	if (!SrcRHI.IsValid())
	{
		static bool bLoggedNoTex = false;
		if (!bLoggedNoTex)
		{
			bLoggedNoTex = true;
			UE_LOG(LogTSICWebUI, Warning, TEXT("[gpu][%s] tex_id=%u has no RHI texture in driver"),
				*Entry.ViewName.ToString(), RT.texture_id);
		}
		return;
	}

	static bool bLoggedSize = false;
	if (!bLoggedSize)
	{
		bLoggedSize = true;
		UE_LOG(LogTSICWebUI, Log,
			TEXT("[gpu][%s] render target: tex_id=%u %ux%u (texture %ux%u) uv=(%.2f..%.2f, %.2f..%.2f)"),
			*Entry.ViewName.ToString(), RT.texture_id, RT.width, RT.height,
			RT.texture_width, RT.texture_height,
			RT.uv_coords.left, RT.uv_coords.right, RT.uv_coords.top, RT.uv_coords.bottom);
	}

	// Copy the GPU-rendered Ultralight texture into the Slate-facing UTexture2D
	// so the existing brush/SImage path keeps working unchanged.
	const FTextureResource* DstResource = Texture->GetResource();
	FRHITexture* DstRHIRaw = DstResource ? DstResource->TextureRHI.GetReference() : nullptr;
	if (!DstRHIRaw)
	{
		return;
	}
	FTextureRHIRef DstRHI = DstRHIRaw;
	const uint32 W = FMath::Min<uint32>(Texture->GetSizeX(), RT.width);
	const uint32 H = FMath::Min<uint32>(Texture->GetSizeY(), RT.height);
	ENQUEUE_RENDER_COMMAND(TSICWebGPUCopyRT)(
		[SrcRHI, DstRHI, W, H](FRHICommandListImmediate& RHICmdList)
		{
			FRHICopyTextureInfo CopyInfo;
			CopyInfo.Size = FIntVector(W, H, 1);
			RHICmdList.Transition(FRHITransitionInfo(SrcRHI, ERHIAccess::Unknown, ERHIAccess::CopySrc));
			RHICmdList.Transition(FRHITransitionInfo(DstRHI, ERHIAccess::Unknown, ERHIAccess::CopyDest));
			RHICmdList.CopyTexture(SrcRHI, DstRHI, CopyInfo);
			RHICmdList.Transition(FRHITransitionInfo(DstRHI, ERHIAccess::CopyDest, ERHIAccess::SRVGraphics));
		});
}

// --- Ultralight ImageSource bridge ----------------------------------------

namespace
{
	TSICWebUI::FWebGPUDriver* AsTSICDriver(void* InRaw)
	{
		return static_cast<TSICWebUI::FWebGPUDriver*>(InRaw);
	}

	ultralight::String FNameToUL(FName In)
	{
		const FString S = In.ToString();
		FTCHARToUTF8 Conv(*S);
		return ultralight::String(Conv.Get(), Conv.Length());
	}
}

void UTSICWebUISubsystem::RegisterImageSourceFromTexture(FName Identifier, UTexture2D* Tex)
{
	check(IsInGameThread());
	if (Identifier.IsNone())
	{
		UE_LOG(LogTSICWebUI, Warning, TEXT("RegisterImageSourceFromTexture: empty identifier rejected."));
		return;
	}
	if (!IsValid(Tex))
	{
		UE_LOG(LogTSICWebUI, Warning, TEXT("RegisterImageSourceFromTexture('%s'): null texture rejected."),
			*Identifier.ToString());
		return;
	}
	TSICWebUI::FWebGPUDriver* Driver = AsTSICDriver(GPUDriverPtr);
	if (!Driver)
	{
		UE_LOG(LogTSICWebUI, Warning, TEXT("RegisterImageSourceFromTexture('%s'): GPU driver not initialised; aborting."),
			*Identifier.ToString());
		return;
	}

	FTextureResource* Resource = Tex->GetResource();
	if (!Resource || !Resource->TextureRHI.IsValid())
	{
		UE_LOG(LogTSICWebUI, Warning,
			TEXT("RegisterImageSourceFromTexture('%s'): texture resource not yet created. Call after UpdateResource() has completed."),
			*Identifier.ToString());
		return;
	}
	FTextureRHIRef RHI = Resource->TextureRHI;
	const uint32 W = static_cast<uint32>(Tex->GetSizeX());
	const uint32 H = static_cast<uint32>(Tex->GetSizeY());

	// Drop any previous binding for this identifier.
	UnregisterImageSource(Identifier);

	const uint32 NewTexId = Driver->RegisterExternalTexture(RHI, W, H);
	if (NewTexId == 0)
	{
		UE_LOG(LogTSICWebUI, Warning, TEXT("RegisterImageSourceFromTexture('%s'): driver refused texture (id=0)."),
			*Identifier.ToString());
		return;
	}

	const ultralight::Rect UV = { 0.f, 0.f, 1.f, 1.f };
	ultralight::RefPtr<ultralight::ImageSource> Source =
		ultralight::ImageSource::CreateFromTexture(W, H, NewTexId, UV, /*bitmap*/ nullptr);

	if (!Source)
	{
		UE_LOG(LogTSICWebUI, Error, TEXT("RegisterImageSourceFromTexture('%s'): ImageSource::CreateFromTexture returned null."),
			*Identifier.ToString());
		Driver->UnregisterExternalTexture(NewTexId);
		return;
	}

	ultralight::ImageSourceProvider::instance().AddImageSource(FNameToUL(Identifier), Source);

	FImageSourceEntry Entry;
	Entry.TextureId = NewTexId;
	Entry.Tex = Tex;
	// Hold a strong ref by retaining the raw pointer with an extra AddRef.
	Source->AddRef();
	Entry.ImageSource = Source.get();
	ImageSourceBridges.Add(Identifier, MoveTemp(Entry));

	UE_LOG(LogTSICWebUI, Log, TEXT("RegisterImageSourceFromTexture('%s'): registered tex id=%u (%ux%u)"),
		*Identifier.ToString(), NewTexId, W, H);
}

void UTSICWebUISubsystem::InvalidateImageSource(FName Identifier)
{
	check(IsInGameThread());
	FImageSourceEntry* Entry = ImageSourceBridges.Find(Identifier);
	if (!Entry || !Entry->ImageSource)
	{
		return;
	}
	static_cast<ultralight::ImageSource*>(Entry->ImageSource)->Invalidate();
}

void UTSICWebUISubsystem::UnregisterImageSource(FName Identifier)
{
	check(IsInGameThread());
	FImageSourceEntry Entry;
	if (!ImageSourceBridges.RemoveAndCopyValue(Identifier, Entry))
	{
		return;
	}

	ultralight::ImageSourceProvider::instance().RemoveImageSource(FNameToUL(Identifier));

	if (Entry.ImageSource)
	{
		// Drop our retained AddRef from Register.
		static_cast<ultralight::ImageSource*>(Entry.ImageSource)->Release();
	}

	if (TSICWebUI::FWebGPUDriver* Driver = AsTSICDriver(GPUDriverPtr))
	{
		Driver->UnregisterExternalTexture(Entry.TextureId);
	}

	UE_LOG(LogTSICWebUI, Log, TEXT("UnregisterImageSource('%s'): cleared tex id=%u"),
		*Identifier.ToString(), Entry.TextureId);
}

// --- Web Gamepad API forwarding ----------------------------------------

void UTSICWebUISubsystem::FireGamepadConnectionEvent(uint32 SlotIndex, bool bConnected,
	uint32 AxisCount, uint32 ButtonCount)
{
	if (!bIsInitialized || !RendererPtr)
	{
		return;
	}
	ultralight::Renderer* Renderer = static_cast<ultralight::Renderer*>(RendererPtr);

	// SetGamepadDetails is required before the first Fire* call.
	ultralight::String Id(TCHAR_TO_UTF8(TEXT("XInput Compatible")));
	Renderer->SetGamepadDetails(SlotIndex, Id, AxisCount, ButtonCount);

	ultralight::GamepadEvent Ev;
	Ev.type = bConnected
		? ultralight::GamepadEvent::kType_GamepadConnected
		: ultralight::GamepadEvent::kType_GamepadDisconnected;
	Ev.index = SlotIndex;
	Renderer->FireGamepadEvent(Ev);
}

void UTSICWebUISubsystem::FireGamepadAxis(uint32 SlotIndex, uint32 AxisIndex, double Value)
{
	if (!bIsInitialized || !RendererPtr)
	{
		return;
	}
	ultralight::GamepadAxisEvent Ev;
	Ev.index = SlotIndex;
	Ev.axis_index = AxisIndex;
	Ev.value = Value;
	static_cast<ultralight::Renderer*>(RendererPtr)->FireGamepadAxisEvent(Ev);
}

void UTSICWebUISubsystem::FireGamepadButton(uint32 SlotIndex, uint32 ButtonIndex, double Value)
{
	if (!bIsInitialized || !RendererPtr)
	{
		return;
	}
	ultralight::GamepadButtonEvent Ev;
	Ev.index = SlotIndex;
	Ev.button_index = ButtonIndex;
	Ev.value = Value;
	static_cast<ultralight::Renderer*>(RendererPtr)->FireGamepadButtonEvent(Ev);
}
