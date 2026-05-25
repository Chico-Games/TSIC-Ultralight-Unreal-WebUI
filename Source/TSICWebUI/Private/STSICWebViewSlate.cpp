#include "STSICWebViewSlate.h"
#include "TSICWebUI.h"
#include "TSICWebUISubsystem.h"
#include "TSICWebUITypes.h"

#include "CoreGlobals.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Rendering/DrawElements.h"
#include "TextureResource.h"
#include "Widgets/Images/SImage.h"

void STSICWebViewSlate::Construct(const FArguments& InArgs)
{
	ViewName = InArgs._ViewName;
	DesiredSize = InArgs._DesiredSize;
	Brush = FSlateBrush();
	Brush.DrawAs = ESlateBrushDrawType::Image;

	// Placeholder brush — drawn while waiting for the Ultralight texture's
	// RHI resource to come online. SImage's default brush is the engine
	// checker pattern; we explicitly override with a no-draw brush so the
	// first paint pre-Tick doesn't flash checker.
	EmptyBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
	EmptyBrush.TintColor = FSlateColor(FLinearColor::Transparent);

	// SImage child hosts the live texture. Rebinding its brush every Tick via
	// FDeferredCleanupSlateBrush::CreateBrush + SetImage is what actually makes
	// the live Ultralight texture visible — SImage::SetImage invalidates Slate
	// internally each frame. Pure SWidget cache-bust paths (ForceVolatile,
	// Invalidate-in-Tick, SetResourceObject(nullptr) in OnPaint) all fail to
	// repaint the texture under global invalidation. Reinstated from commit
	// 5a331473f; the 203a55114 revert dropped this and that's the regression
	// the user observed as "screenshot shows magenta, normal play doesn't".
	ChildImage = SNew(SImage).Image(&EmptyBrush);
	ChildSlot[ChildImage.ToSharedRef()];

	ForceVolatile(true);
	SetCanTick(true);

	// Hand the subsystem a weak ref so SetFocusCapture() can route Slate user
	// focus to us when the director (or BP) asks for keyboard capture.
	if (UTSICWebUISubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->RegisterSlateWidget(ViewName, AsWeak());
	}
}

STSICWebViewSlate::~STSICWebViewSlate()
{
	if (UTSICWebUISubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->UnregisterSlateWidget(ViewName);
	}
}

void STSICWebViewSlate::Tick(const FGeometry& AllottedGeometry, const double /*InCurrentTime*/, const float /*InDeltaTime*/)
{
	UTSICWebUISubsystem* Subsystem = GetSubsystem();

	// One-shot diagnostic: dump the world-resolution state the very first time
	// Tick fires for this widget. Targets the embedded-vs-floating PIE asymmetry —
	// floating PIE renders the Ultralight texture, embedded "Selected Viewport"
	// PIE doesn't, and GetSubsystem() relies on GEngine->GetCurrentPlayWorld()
	// which is gated by UE::GetPlayInEditorID().
	if (!bDiagLoggedFirstTick)
	{
		bDiagLoggedFirstTick = true;
		const int32 PIEId = UE::GetPlayInEditorID();
		UWorld* const CurPlayWorld = GEngine ? GEngine->GetCurrentPlayWorld() : nullptr;
		const FVector2D LocalSizeDbg = AllottedGeometry.GetLocalSize();
		UE_LOG(LogTSICWebUI, Warning,
			TEXT("[diag] Tick#1 view='%s' size=%.0fx%.0f scale=%.3f PIEId=%d CurrentPlayWorld=%s SubsystemFound=%d"),
			*ViewName.ToString(),
			LocalSizeDbg.X, LocalSizeDbg.Y,
			AllottedGeometry.Scale,
			PIEId,
			*GetNameSafe(CurPlayWorld),
			Subsystem != nullptr ? 1 : 0);

		if (GEngine)
		{
			const TIndirectArray<FWorldContext>& Contexts = GEngine->GetWorldContexts();
			UE_LOG(LogTSICWebUI, Warning, TEXT("[diag] Tick#1 WorldContexts count=%d"), Contexts.Num());
			for (int32 i = 0; i < Contexts.Num(); ++i)
			{
				const FWorldContext& Ctx = Contexts[i];
				UE_LOG(LogTSICWebUI, Warning,
					TEXT("[diag]   [%d] WorldType=%d PIEInstance=%d PrimaryPIE=%d World=%s GI=%s Viewport=%s"),
					i,
					static_cast<int32>(Ctx.WorldType.GetValue()),
					Ctx.PIEInstance,
					Ctx.bIsPrimaryPIEInstance ? 1 : 0,
					*GetNameSafe(Ctx.World()),
					*GetNameSafe(Ctx.OwningGameInstance),
					*GetNameSafe(Ctx.GameViewport));
			}
		}
	}

	if (!Subsystem)
	{
		return;
	}

	if (!bDiagLoggedFirstReady)
	{
		bDiagLoggedFirstReady = true;
		UE_LOG(LogTSICWebUI, Warning,
			TEXT("[diag] Subsystem first reachable for view='%s' PIEId=%d"),
			*ViewName.ToString(),
			UE::GetPlayInEditorID());
	}

	// Auto-resize the underlying Ultralight view to match this widget's on-screen
	// pixel size so the page renders 1:1 (avoids smudging from texture upscale).
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const float Scale = AllottedGeometry.Scale;
	const int32 PixelW = FMath::Max(1, FMath::RoundToInt(LocalSize.X * Scale));
	const int32 PixelH = FMath::Max(1, FMath::RoundToInt(LocalSize.Y * Scale));
	// Hysteresis: only resize when the size changes by more than a few pixels,
	// otherwise sub-pixel geometry jitter would recreate the texture every frame.
	const int32 Tol = 4;
	if (FMath::Abs(PixelW - LastResizeW) > Tol || FMath::Abs(PixelH - LastResizeH) > Tol)
	{
		Subsystem->ResizeView(ViewName, PixelW, PixelH);
		LastResizeW = PixelW;
		LastResizeH = PixelH;
	}

	// Rebind the SImage child's brush against the live texture every frame.
	// Guards required to avoid the engine default checker fallback:
	//   - GetResource() non-null       — FTextureResource allocated
	//   - GetResource()->TextureRHI    — RHI texture actually initialised on
	//     the render thread (UTexture2D::UpdateResource is async; the
	//     FTextureResource exists synchronously but TextureRHI is created on
	//     the render thread, and SImage will fall back to the checker if we
	//     hand it a not-yet-RHI-valid texture).
	UTexture2D* Texture = Subsystem->GetViewTexture(ViewName);
	const bool bRHIReady = Texture && Texture->GetResource() && Texture->GetResource()->TextureRHI.IsValid();
	if (bRHIReady && ChildImage.IsValid())
	{
		DeferredBrush = FDeferredCleanupSlateBrush::CreateBrush(
			Texture,
			FVector2D(Texture->GetSizeX(), Texture->GetSizeY()),
			FLinearColor::White,
			ESlateBrushTileType::NoTile,
			ESlateBrushImageType::FullColor);
		if (DeferredBrush.IsValid())
		{
			ChildImage->SetImage(DeferredBrush->GetSlateBrush());
		}
	}
	else if (ChildImage.IsValid())
	{
		// Texture not yet GPU-ready: render nothing so we don't show the
		// engine default checker pattern while waiting for the first frame.
		ChildImage->SetImage(&EmptyBrush);
	}

	// Per-frame Slate invalidation: SImage::SetImage with a fresh
	// FDeferredCleanupSlateBrush pointer *should* invalidate on its own, but
	// in practice (verified empirically: "shot showui must run twice for the
	// page to appear") the cached paint output sticks until an external Slate
	// event invalidates layout. Force the full set every Tick so the live
	// texture actually surfaces on the very next frame without needing a
	// screenshot to kick the chain.
	if (ChildImage.IsValid())
	{
		ChildImage->Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
	}
	Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint | EInvalidateWidgetReason::PaintAndVolatility);
}

UTSICWebUISubsystem* STSICWebViewSlate::GetSubsystem() const
{
	if (!GEngine)
	{
		return nullptr;
	}
	UWorld* World = GEngine->GetCurrentPlayWorld();
	if (!World)
	{
		return nullptr;
	}
	UGameInstance* GameInstance = World->GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UTSICWebUISubsystem>() : nullptr;
}

bool STSICWebViewSlate::ResolveLocal(const FGeometry& MyGeometry, FVector2D ScreenPos, int32& OutX, int32& OutY) const
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(ScreenPos);
	const FVector2D Size = MyGeometry.GetLocalSize();
	if (Size.X <= 0.f || Size.Y <= 0.f)
	{
		return false;
	}

	UTSICWebUISubsystem* Subsystem = GetSubsystem();
	UTexture2D* Texture = Subsystem ? Subsystem->GetViewTexture(ViewName) : nullptr;
	if (!Texture)
	{
		return false;
	}

	const float ScaleX = static_cast<float>(Texture->GetSizeX()) / static_cast<float>(Size.X);
	const float ScaleY = static_cast<float>(Texture->GetSizeY()) / static_cast<float>(Size.Y);
	OutX = FMath::Clamp(FMath::FloorToInt(Local.X * ScaleX), 0, static_cast<int32>(Texture->GetSizeX()) - 1);
	OutY = FMath::Clamp(FMath::FloorToInt(Local.Y * ScaleY), 0, static_cast<int32>(Texture->GetSizeY()) - 1);
	return true;
}

bool STSICWebViewSlate::ShouldForwardMouseAt(const FGeometry& MyGeometry, FVector2D ScreenPos, int32& OutX, int32& OutY) const
{
	if (!ResolveLocal(MyGeometry, ScreenPos, OutX, OutY))
	{
		return false;
	}
	UTSICWebUISubsystem* Subsystem = GetSubsystem();
	return Subsystem && Subsystem->IsPointInteractive(ViewName, OutX, OutY);
}

bool STSICWebViewSlate::ShouldForwardKey() const
{
	UTSICWebUISubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return false;
	}
	if (Subsystem->GetFocusMode(ViewName) == EWebViewFocusMode::MouseOnly)
	{
		return false;
	}
	return Subsystem->ViewHasFocus(ViewName);
}

int32 STSICWebViewSlate::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Delegate to SCompoundWidget which paints our SImage child whose brush
	// is rebound every Tick (see ::Tick).
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

FVector2D STSICWebViewSlate::ComputeDesiredSize(float /*LayoutScaleMultiplier*/) const
{
	UTSICWebUISubsystem* Subsystem = GetSubsystem();
	if (Subsystem)
	{
		if (UTexture2D* Tex = Subsystem->GetViewTexture(ViewName))
		{
			return FVector2D(static_cast<float>(Tex->GetSizeX()), static_cast<float>(Tex->GetSizeY()));
		}
	}
	return DesiredSize;
}

bool STSICWebViewSlate::SupportsKeyboardFocus() const
{
	return true;
}

FReply STSICWebViewSlate::OnFocusReceived(const FGeometry& /*MyGeometry*/, const FFocusEvent& /*InFocusEvent*/)
{
	if (UTSICWebUISubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->FocusView(ViewName);
	}
	return FReply::Handled();
}

void STSICWebViewSlate::OnFocusLost(const FFocusEvent& /*InFocusEvent*/)
{
	if (UTSICWebUISubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->UnfocusView(ViewName);
	}
}

FReply STSICWebViewSlate::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	int32 X, Y;
	if (!ShouldForwardMouseAt(MyGeometry, MouseEvent.GetScreenSpacePosition(), X, Y))
	{
		return FReply::Unhandled();
	}
	if (UTSICWebUISubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->FireMouseMove(ViewName, X, Y);
	}
	return FReply::Handled();
}

FReply STSICWebViewSlate::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	int32 X, Y;
	if (!ShouldForwardMouseAt(MyGeometry, MouseEvent.GetScreenSpacePosition(), X, Y))
	{
		return FReply::Unhandled();
	}

	UTSICWebUISubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FReply::Unhandled();
	}

	Subsystem->FireMouseButton(ViewName, X, Y, MouseEvent.GetEffectingButton(), true);

	FReply Reply = FReply::Handled();
	Reply.CaptureMouse(SharedThis(this));

	// Only steal keyboard focus for modes that want it. MouseOnly/ManualOnly never grab it on click,
	// preserving the player controller's Enhanced Input bindings.
	const EWebViewFocusMode Mode = Subsystem->GetFocusMode(ViewName);
	if (Mode == EWebViewFocusMode::CaptureOnClick || Mode == EWebViewFocusMode::AlwaysCaptured)
	{
		Reply.SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}
	return Reply;
}

FReply STSICWebViewSlate::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	int32 X, Y;
	const bool bWasInteractive = ShouldForwardMouseAt(MyGeometry, MouseEvent.GetScreenSpacePosition(), X, Y);
	UTSICWebUISubsystem* Subsystem = GetSubsystem();
	if (Subsystem)
	{
		if (!bWasInteractive)
		{
			// Resolve coordinates even outside interactive area so the engine receives the matching MouseUp.
			ResolveLocal(MyGeometry, MouseEvent.GetScreenSpacePosition(), X, Y);
		}
		Subsystem->FireMouseButton(ViewName, X, Y, MouseEvent.GetEffectingButton(), false);
	}

	FReply Reply = FReply::Handled();
	if (HasMouseCapture())
	{
		Reply.ReleaseMouseCapture();
	}
	return Reply;
}

FReply STSICWebViewSlate::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	int32 X, Y;
	if (!ShouldForwardMouseAt(MyGeometry, MouseEvent.GetScreenSpacePosition(), X, Y))
	{
		return FReply::Unhandled();
	}
	if (UTSICWebUISubsystem* Subsystem = GetSubsystem())
	{
		// Slate's WheelDelta is +1 per notch; convert to a reasonable pixel scroll.
		constexpr int32 PixelsPerNotch = 60;
		Subsystem->FireMouseWheel(ViewName, 0, FMath::RoundToInt(MouseEvent.GetWheelDelta() * PixelsPerNotch));
	}
	return FReply::Handled();
}

FReply STSICWebViewSlate::OnKeyDown(const FGeometry& /*MyGeometry*/, const FKeyEvent& InKeyEvent)
{
	UTSICWebUISubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FReply::Unhandled();
	}

	// Pressing the release key returns Slate focus to the game viewport so Enhanced Input
	// resumes receiving keys. AlwaysCaptured mode does not honour the release key.
	const EWebViewFocusMode Mode = Subsystem->GetFocusMode(ViewName);
	if (Mode != EWebViewFocusMode::AlwaysCaptured
		&& InKeyEvent.GetKey() == Subsystem->GetFocusReleaseKey(ViewName)
		&& Subsystem->ViewHasFocus(ViewName))
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
		return FReply::Handled();
	}

	if (!ShouldForwardKey())
	{
		return FReply::Unhandled();
	}
	Subsystem->FireKeyEvent(ViewName, InKeyEvent, true);
	return FReply::Handled();
}

FReply STSICWebViewSlate::OnKeyUp(const FGeometry& /*MyGeometry*/, const FKeyEvent& InKeyEvent)
{
	if (!ShouldForwardKey())
	{
		return FReply::Unhandled();
	}
	if (UTSICWebUISubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->FireKeyEvent(ViewName, InKeyEvent, false);
	}
	return FReply::Handled();
}

FReply STSICWebViewSlate::OnKeyChar(const FGeometry& /*MyGeometry*/, const FCharacterEvent& InCharacterEvent)
{
	if (!ShouldForwardKey())
	{
		return FReply::Unhandled();
	}
	if (UTSICWebUISubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->FireCharEvent(ViewName, InCharacterEvent);
	}
	return FReply::Handled();
}

FCursorReply STSICWebViewSlate::OnCursorQuery(const FGeometry& /*MyGeometry*/, const FPointerEvent& /*CursorEvent*/) const
{
	UTSICWebUISubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FCursorReply::Unhandled();
	}
	return FCursorReply::Cursor(Subsystem->GetCursorForView(ViewName));
}
