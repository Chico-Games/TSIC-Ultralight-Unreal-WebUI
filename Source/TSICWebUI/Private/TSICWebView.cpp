#include "TSICWebView.h"
#include "STSICWebViewSlate.h"
#include "TSICWebUI.h"
#include "TSICWebUISubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "TSICWebView"

UTSICWebUISubsystem* UTSICWebView::GetSubsystem() const
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

TSharedRef<SWidget> UTSICWebView::RebuildWidget()
{
	SlateWidget = SNew(STSICWebViewSlate)
		.ViewName(ViewName)
		.DesiredSize(FVector2D(ViewConfig.Width, ViewConfig.Height));

	EnsureViewCreated();
	return SlateWidget.ToSharedRef();
}

void UTSICWebView::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	if (bViewCreated)
	{
		if (UTSICWebUISubsystem* Subsystem = GetSubsystem())
		{
			Subsystem->DestroyView(ViewName);
		}
	}

	bViewCreated = false;
	SlateWidget.Reset();
}

void UTSICWebView::EnsureViewCreated()
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

void UTSICWebView::LoadURL(const FString& URL)
{
	if (UTSICWebUISubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->LoadURL(ViewName, URL);
	}
}

void UTSICWebView::LoadHTML(const FString& Html)
{
	if (UTSICWebUISubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->LoadHTML(ViewName, Html);
	}
}

void UTSICWebView::SetFocusCapture(bool bCapture)
{
	if (UTSICWebUISubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->SetFocusCapture(ViewName, bCapture);
	}
}

void UTSICWebView::ReleaseFocus()
{
	if (UTSICWebUISubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->UnfocusView(ViewName);
	}
}

bool UTSICWebView::IsFocused() const
{
	UTSICWebUISubsystem* Subsystem = GetSubsystem();
	return Subsystem && Subsystem->ViewHasFocus(ViewName);
}

void UTSICWebView::SetInteractiveRects(const TArray<FTSICWebInteractiveRect>& Rects)
{
	if (UTSICWebUISubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->SetInteractiveRects(ViewName, Rects);
	}
}

void UTSICWebView::BroadcastEvent(FName ChannelName, const FString& PayloadJson)
{
	if (UTSICWebUISubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->BroadcastEvent(ChannelName, PayloadJson);
	}
}

#if WITH_EDITOR
const FText UTSICWebView::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "TSIC | Web UI");
}
#endif

#undef LOCTEXT_NAMESPACE
