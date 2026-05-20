#include "Misc/AutomationTest.h"
#include "TSICWebUISubsystem.h"
#include "GameplayTagsManager.h"
#include "NativeGameplayTags.h"
#include "Engine/GameInstance.h"

// Declare test tags as native gameplay tags
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Test_Cache_Write);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Test_Cache_Transient);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Test_Cache_Overwrite);

namespace
{
	// Construct the subsystem with a minimal GameInstance outer. We deliberately
	// skip Initialize() because we only need the cache map + BroadcastBridgedMessage
	// logic, both of which are independent of the Ultralight platform and event bus.
	// BroadcastEvent inside the helper no-ops when EventBus is null, which is exactly
	// the state when Initialize is skipped.
	UTSICWebUISubsystem* MakeSubsystem()
	{
		UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
		GI->AddToRoot();
		UTSICWebUISubsystem* S = NewObject<UTSICWebUISubsystem>(GI);
		S->AddToRoot();
		return S;
	}

	void DestroySubsystem(UTSICWebUISubsystem* S)
	{
		if (S)
		{
			S->RemoveFromRoot();
			if (UGameInstance* GI = Cast<UGameInstance>(S->GetOuter()))
			{
				GI->RemoveFromRoot();
			}
		}
	}

	FGameplayTag GetTestTag(uint32 Index)
	{
		switch (Index)
		{
			case 1: return TAG_Test_Cache_Write;
			case 2: return TAG_Test_Cache_Transient;
			case 3: return TAG_Test_Cache_Overwrite;
			default: return TAG_Test_Cache_Write;
		}
	}
}

// Define the test tags
UE_DEFINE_GAMEPLAY_TAG(TAG_Test_Cache_Write, "Test.Cache.Write");
UE_DEFINE_GAMEPLAY_TAG(TAG_Test_Cache_Transient, "Test.Cache.Transient");
UE_DEFINE_GAMEPLAY_TAG(TAG_Test_Cache_Overwrite, "Test.Cache.Overwrite");

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTSICWebUICacheTest_BroadcastWritesEntry,
    "TSIC.WebUI.Cache.BroadcastWritesEntry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTSICWebUICacheTest_BroadcastWritesEntry::RunTest(const FString& /*Parameters*/)
{
	UTSICWebUISubsystem* S = MakeSubsystem();
	const FGameplayTag Tag = GetTestTag(1);
	S->BroadcastBridgedMessage(Tag, FName(TEXT("tsic.msg.test.write")), TEXT("{\"x\":1}"), /*bTransient*/ false);

	const FTimespan Age = S->GetCachedAge(Tag);
	TestTrue(TEXT("Cache age must be finite after non-transient broadcast"), Age < FTimespan::MaxValue());
	TestTrue(TEXT("Cache age must be tiny right after write"), Age.GetTotalSeconds() < 1.0);

	DestroySubsystem(S);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTSICWebUICacheTest_TransientSkipsCache,
    "TSIC.WebUI.Cache.TransientSkipsCache",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTSICWebUICacheTest_TransientSkipsCache::RunTest(const FString& /*Parameters*/)
{
	UTSICWebUISubsystem* S = MakeSubsystem();
	const FGameplayTag Tag = GetTestTag(2);
	S->BroadcastBridgedMessage(Tag, FName(TEXT("tsic.msg.test.transient")), TEXT("{\"x\":1}"), /*bTransient*/ true);

	const FTimespan Age = S->GetCachedAge(Tag);
	TestEqual(TEXT("Transient broadcast must leave cache empty for this tag"), Age, FTimespan::MaxValue());

	DestroySubsystem(S);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTSICWebUICacheTest_OverwritesByTag,
    "TSIC.WebUI.Cache.OverwritesByTag",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTSICWebUICacheTest_OverwritesByTag::RunTest(const FString& /*Parameters*/)
{
	UTSICWebUISubsystem* S = MakeSubsystem();
	const FGameplayTag Tag = GetTestTag(3);
	S->BroadcastBridgedMessage(Tag, FName(TEXT("tsic.msg.test.ov")), TEXT("{\"x\":1}"), false);
	FPlatformProcess::Sleep(0.1f);
	const FTimespan First = S->GetCachedAge(Tag);
	FPlatformProcess::Sleep(0.1f);
	S->BroadcastBridgedMessage(Tag, FName(TEXT("tsic.msg.test.ov")), TEXT("{\"x\":2}"), false);
	const FTimespan Second = S->GetCachedAge(Tag);
	TestTrue(TEXT("Overwrite should produce a fresher (smaller) age"), Second < First);

	DestroySubsystem(S);
	return true;
}
