#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "TSICWebUITypes.h"
#include "TSICWebView.generated.h"

class UTSICWebUISubsystem;
class STSICWebViewSlate;

UCLASS()
class TSICWEBUI_API UTSICWebView : public UWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TSIC Web UI")
	FTSICWebViewConfig ViewConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TSIC Web UI")
	FName ViewName = TEXT("DefaultView");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TSIC Web UI")
	FString InitialURL = TEXT("file:///test.html");

	UFUNCTION(BlueprintCallable, Category = "TSIC Web UI")
	void LoadURL(const FString& URL);

	UFUNCTION(BlueprintCallable, Category = "TSIC Web UI")
	void LoadHTML(const FString& Html);

	UFUNCTION(BlueprintCallable, Category = "TSIC Web UI|Input")
	void SetFocusCapture(bool bCapture);

	UFUNCTION(BlueprintCallable, Category = "TSIC Web UI|Input")
	void ReleaseFocus();

	UFUNCTION(BlueprintCallable, Category = "TSIC Web UI|Input", BlueprintPure)
	bool IsFocused() const;

	UFUNCTION(BlueprintCallable, Category = "TSIC Web UI|Input")
	void SetInteractiveRects(const TArray<FTSICWebInteractiveRect>& Rects);

	UFUNCTION(BlueprintCallable, Category = "TSIC Web UI|Events")
	void BroadcastEvent(FName ChannelName, const FString& PayloadJson);

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UTSICWebUISubsystem* GetSubsystem() const;
	void EnsureViewCreated();

	TSharedPtr<STSICWebViewSlate> SlateWidget;
	bool bViewCreated = false;
};
