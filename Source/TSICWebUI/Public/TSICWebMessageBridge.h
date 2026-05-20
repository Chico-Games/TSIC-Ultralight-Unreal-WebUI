#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameplayTagContainer.h"
#include "JsonObjectConverter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "TSICWebUISubsystem.h"

// Per-bridge info kept on the subsystem so `tsic.describeMessages()` and
// `WebUI.DumpMessages` can enumerate everything that's available.
struct FTSICWebMessageBridgeInfo
{
	FGameplayTag SourceTag;
	FName BusChannel;            // e.g. "tsic.msg.TSIC.Message.DamageEvent"
	FString StructName;          // e.g. "ScpDamageEventMessage"
	FString Description;
	TArray<FString> FieldNames;  // reflection-walked authored property names
	FGameplayMessageListenerHandle ListenerHandle;

	// Cache-bypass hint. When true, broadcasts of this tag are NOT written to
	// UTSICWebUISubsystem::CachedPayloadJson, so they will not be replayed when
	// a JS context comes online. Use for true one-shot events like toasts.
	bool bTransient = false;

	// JSON -> UStruct -> BroadcastMessage<TStruct>. Captured at registration time
	// (templated path) so we keep type fidelity through the reflection round-trip.
	// Returns false if the JSON failed to deserialize.
	TFunction<bool(FGameplayTag /*Tag*/, const FString& /*Json*/, UObject* /*WorldCtx*/)> BroadcastFromJson;
};

namespace TSICWebUI
{
	// Walk a USTRUCT's properties and return the authored property names.
	// Skips Transient properties to avoid noise.
	TSICWEBUI_API TArray<FString> CollectStructFieldNames(const UScriptStruct* Struct);

	// Build a bus channel FName from a gameplay tag, prefixed `tsic.msg.`.
	TSICWEBUI_API FName MakeMessageChannelName(FGameplayTag Tag);
}

// Templated helper to wire a (FGameplayTag, USTRUCT) pair from
// UGameplayMessageSubsystem into the TSICWebUI event bus, with automatic
// JSON serialization.
//
// Usage from any C++ module (game or mod):
//   UTSICWebUISubsystem* WebUI = ...;
//   TSICWebBridgeMessage<FScpDamageEventMessage>(WebUI,
//       ScpGameplayTags::Message_DamageEvent,
//       TEXT("Player or enemy damage event"));
template <typename TStruct>
void TSICWebBridgeMessage(UTSICWebUISubsystem* Subsystem, FGameplayTag Tag, const FString& Description = FString(), bool bTransient = false)
{
	if (!Subsystem || !Tag.IsValid())
	{
		return;
	}

	const FName Channel = TSICWebUI::MakeMessageChannelName(Tag);

	// Register the bus channel that pages will subscribe to.
	Subsystem->RegisterChannel(Channel, EWebChannelKind::Event,
		Description.IsEmpty()
			? FString::Printf(TEXT("Bridged from gameplay message %s (struct %s)"),
				*Tag.ToString(), *TStruct::StaticStruct()->GetName())
			: Description);

	// Register a listener with the gameplay message subsystem.
	UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(Subsystem);
	TWeakObjectPtr<UTSICWebUISubsystem> WeakSub(Subsystem);
	FGameplayMessageListenerHandle Handle = MessageSubsystem.RegisterListener<TStruct>(Tag,
		[WeakSub, Tag, Channel, bTransient](FGameplayTag /*ActualTag*/, const TStruct& Payload)
		{
			UTSICWebUISubsystem* S = WeakSub.Get();
			if (!S)
			{
				return;
			}
			FString PayloadJson;
			// SkipStandardizeCase preserves the authored PascalCase property
			// names (FName Name -> "Name", not "name"). JS callers across the
			// SPA read payload.Name, payload.Mode, payload.Slots etc.; without
			// this flag UE's default down-cases the first character and every
			// `payload.X` access silently returns undefined. UI.Cmd.Menu.Navigate
			// was the canary — its router check `if (!payload.Name) return;`
			// short-circuited every nav click.
			FJsonObjectConverter::UStructToFormattedJsonObjectString<TCHAR, TCondensedJsonPrintPolicy>(
				TStruct::StaticStruct(),
				&Payload,
				PayloadJson,
				/*CheckFlags*/ 0,
				/*SkipFlags*/ 0,
				/*Indent*/ 0,
				/*ExportCb*/ nullptr,
				EJsonObjectConversionFlags::SkipStandardizeCase);
			S->BroadcastBridgedMessage(Tag, Channel, PayloadJson, bTransient);
		});

	FTSICWebMessageBridgeInfo Info;
	Info.SourceTag = Tag;
	Info.BusChannel = Channel;
	Info.StructName = TStruct::StaticStruct()->GetName();
	Info.Description = Description;
	Info.FieldNames = TSICWebUI::CollectStructFieldNames(TStruct::StaticStruct());
	Info.ListenerHandle = Handle;
	Info.bTransient = bTransient;
	Info.BroadcastFromJson = [](FGameplayTag InTag, const FString& Json, UObject* WorldCtx) -> bool
	{
		if (!WorldCtx)
		{
			return false;
		}
		TStruct Payload{};
		if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Payload, /*CheckFlags*/ 0, /*SkipFlags*/ 0))
		{
			return false;
		}
		UGameplayMessageSubsystem::Get(WorldCtx).BroadcastMessage(InTag, Payload);
		return true;
	};
	Subsystem->RegisterMessageBridge(MoveTemp(Info));
}
