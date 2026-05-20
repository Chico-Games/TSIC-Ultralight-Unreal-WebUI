#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TSICWebUITypes.h"
#include "TSICWebUIViewportWidget.generated.h"

class STSICWebViewSlate;
class UTSICWebUISubsystem;

/**
 * Persistent UUserWidget that hosts a single full-viewport STSICWebViewSlate
 * driven by UTSICWebUISubsystem. Created once by UScpUIDirectorSubsystem and
 * added to GEngine->GameViewport so it survives map transitions.
 */
UCLASS()
class TSICWEBUI_API UTSICWebUIViewportWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UTSICWebUIViewportWidget(const FObjectInitializer& ObjectInitializer);

	/** Name of the underlying TSICWebUI view this widget owns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TSIC Web UI")
	FName ViewName = TEXT("Root");

	/** URL loaded once on construction. Use a file:// URL pointing at the SPA. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TSIC Web UI")
	FString InitialURL = TEXT("file:///screens/main-menu.html");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TSIC Web UI")
	FTSICWebViewConfig ViewConfig;

	/** Asks the subsystem to load a fresh URL into this widget's underlying view. */
	UFUNCTION(BlueprintCallable, Category = "TSIC Web UI")
	void LoadURL(const FString& URL);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	UTSICWebUISubsystem* GetSubsystem() const;
	void EnsureViewCreated();

	TSharedPtr<STSICWebViewSlate> SlateWidget;
	bool bViewCreated = false;
};
