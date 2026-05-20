#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/ICursor.h"
#include "Slate/DeferredCleanupSlateBrush.h"
#include "Styling/SlateBrush.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SImage;
class UTSICWebUISubsystem;

class STSICWebViewSlate : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STSICWebViewSlate)
		: _ViewName(NAME_None)
		, _DesiredSize(FVector2D(1280.f, 720.f))
	{}
		SLATE_ARGUMENT(FName, ViewName)
		SLATE_ARGUMENT(FVector2D, DesiredSize)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~STSICWebViewSlate() override;

	//~ SWidget overrides
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual bool SupportsKeyboardFocus() const override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	//~

private:
	UTSICWebUISubsystem* GetSubsystem() const;
	bool ResolveLocal(const FGeometry& MyGeometry, FVector2D ScreenPos, int32& OutX, int32& OutY) const;
	bool ShouldForwardMouseAt(const FGeometry& MyGeometry, FVector2D ScreenPos, int32& OutX, int32& OutY) const;
	bool ShouldForwardKey() const;

	FName ViewName;
	FVector2D DesiredSize;
	mutable FSlateBrush Brush;
	// Placeholder brush rendered while the Ultralight texture isn't yet
	// RHI-ready. DrawAs::NoDrawType means SImage draws nothing — preventing
	// the engine default checker-texture fallback that SImage would otherwise
	// show for its default brush before SetImage() is ever called.
	FSlateBrush EmptyBrush;
	// SImage child hosts the live texture. We rebind its brush every Tick with
	// a fresh DeferredCleanupSlateBrush — that's what forces Slate to repaint
	// the live Ultralight texture each frame (SImage::SetImage invalidates
	// internally). Pure SWidget cache-bust paths get stuck under global
	// invalidation; this child-rebind pattern is the one that actually works.
	TSharedPtr<SImage> ChildImage;
	mutable TSharedPtr<FDeferredCleanupSlateBrush> DeferredBrush;
	int32 LastResizeW = 0;
	int32 LastResizeH = 0;
};
