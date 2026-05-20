#include "TSICWebUIViewportWidget.h"

#include "Engine/GameInstance.h"
#include "STSICWebViewSlate.h"
#include "TSICWebUI.h"
#include "TSICWebUISubsystem.h"
#include "Widgets/SNullWidget.h"

UTSICWebUIViewportWidget::UTSICWebUIViewportWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The persistent SPA viewport is meant to overlay the game world. Default
	// transparent so the world shows through wherever the page itself doesn't
	// paint pixels. Pages that want an opaque background can set their own
	// background color on body/html.
	ViewConfig.bIsTransparent = true;
}

UTSICWebUISubsystem* UTSICWebUIViewportWidget::GetSubsystem() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UTSICWebUISubsystem>() : nullptr;
}

TSharedRef<SWidget> UTSICWebUIViewportWidget::RebuildWidget()
{
	UTSICWebUISubsystem* Subsystem = GetSubsystem();
	if (!Subsystem || !Subsystem->IsReady())
	{
		UE_LOG(LogTSICWebUI, Warning, TEXT("ViewportWidget: subsystem not ready; widget will be empty until next rebuild."));
		return SNullWidget::NullWidget;
	}

	SlateWidget = SNew(STSICWebViewSlate)
		.ViewName(ViewName)
		.DesiredSize(FVector2D(ViewConfig.Width, ViewConfig.Height));

	EnsureViewCreated();

	// UUserWidget wraps the returned slate widget in an SObjectWidget which
	// caches paint output between invalidations. Our STSICWebViewSlate redraws
	// from a live Ultralight texture every Tick — the cached UUserWidget output
	// freezes that texture at frame-1 and never repaints despite the child's
	// own invalidation calls. Force the entire UUserWidget chain to be volatile
	// so SObjectWidget skips the cache and OnPaint runs every frame. Without
	// this the SPA only updates when an external Slate invalidation fires
	// (verified: "shot showui must run twice for the page to appear").
	ForceVolatile(true);

	return SlateWidget.ToSharedRef();
}

void UTSICWebUIViewportWidget::EnsureViewCreated()
{
	if (bViewCreated)
	{
		return;
	}

	UTSICWebUISubsystem* Subsystem = GetSubsystem();
	if (!Subsystem || !Subsystem->IsReady())
	{
		return;
	}

	const FName Created = Subsystem->CreateView(ViewName, ViewConfig);
	if (Created.IsNone())
	{
		UE_LOG(LogTSICWebUI, Warning, TEXT("ViewportWidget: CreateView('%s') returned None"), *ViewName.ToString());
		return;
	}

	bViewCreated = true;
	Subsystem->SetFocusMode(ViewName, ViewConfig.FocusMode);
	Subsystem->SetFocusReleaseKey(ViewName, ViewConfig.FocusReleaseKey);

	if (ViewConfig.FocusMode == EWebViewFocusMode::AlwaysCaptured)
	{
		Subsystem->FocusView(ViewName);
	}

	if (!InitialURL.IsEmpty())
	{
		Subsystem->LoadURL(ViewName, InitialURL);
	}
}

void UTSICWebUIViewportWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	SlateWidget.Reset();
}

void UTSICWebUIViewportWidget::LoadURL(const FString& URL)
{
	if (UTSICWebUISubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->LoadURL(ViewName, URL);
	}
}
