#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Delegates/DelegateCombinations.h"
#include "TSICWebUITypes.h"

class UTSICWebUISubsystem;

// Fire-and-forget JS->C++ handler. Receives the channel name and a JSON-encoded payload.
// Invoked on the game thread.
DECLARE_DELEGATE_TwoParams(FTSICWebEventHandler, FName /*Channel*/, const FString& /*PayloadJson*/);

// JS->C++ request handler. Receives channel + JSON payload, returns JSON response.
// Returning an empty optional signals failure (Promise rejects).
// Invoked on the game thread.
DECLARE_DELEGATE_RetVal_TwoParams(TOptional<FString>, FTSICWebRequestHandler, FName /*Channel*/, const FString& /*PayloadJson*/);

struct FTSICWebChannelInfo
{
	FName Name;
	EWebChannelKind Kind = EWebChannelKind::Event;
	FString Description;
	FString LastValueJson;
	bool bHasLastValue = false;
};

struct FTSICWebPendingOutbound
{
	FName Channel;
	FString PayloadJson;
	int32 RequestId = -1; // >=0 means this is a response to a JS request
	FString ErrorMessage; // non-empty + RequestId>=0 means reject the promise
	FName TargetView; // NAME_None = all views
};

struct FTSICWebPendingInbound
{
	FName Channel;
	FString PayloadJson;
	int32 RequestId = -1; // -1 = send, >=0 = request
	FName SourceView; // which view fired this
};

class TSICWEBUI_API FTSICWebEventBus
{
public:
	explicit FTSICWebEventBus(UTSICWebUISubsystem* InOwner);
	~FTSICWebEventBus();

	// --- Channel registration ---

	// Declare a channel. Repeat calls with the same name are idempotent (kind/description are not overwritten).
	// Channels must be registered before BroadcastEvent / RegisterHandler will succeed.
	void RegisterChannel(FName ChannelName, EWebChannelKind Kind, const FString& Description = FString());

	// Subscribe a C++ handler for JS->C++ fire-and-forget events.
	FDelegateHandle RegisterHandler(FName ChannelName, FTSICWebEventHandler Handler);
	void UnregisterHandler(FName ChannelName, FDelegateHandle Handle);

	// Subscribe a C++ handler for JS->C++ requests. Only one handler per channel.
	void RegisterRequestHandler(FName ChannelName, FTSICWebRequestHandler Handler);
	void UnregisterRequestHandler(FName ChannelName);

	// --- C++ -> JS ---

	// Broadcast an event to all JS subscribers on every view. Thread-safe.
	void BroadcastEvent(FName ChannelName, const FString& PayloadJson);

	// --- Inbound from JS bindings (called on game thread inside Renderer::Update) ---

	// Queue an inbound JS->C++ send. Drained on the next Drain() call.
	void EnqueueInboundSend(FName ChannelName, const FString& PayloadJson, FName SourceView);

	// Queue an inbound JS->C++ request. Drained on the next Drain() call.
	void EnqueueInboundRequest(FName ChannelName, const FString& PayloadJson, int32 RequestId, FName SourceView);

	// Called by the subsystem on each tick after Renderer::Update.
	// Drains inbound queues, invokes handlers (on the game thread), then returns the
	// list of outbound items the subsystem should dispatch to JS via EvaluateScript.
	void DrainPending(TArray<FTSICWebPendingOutbound>& OutOutbound);

	// Called by the subsystem when a new view's JS context has just been bound.
	// Returns outbound items to replay sticky-channel last-values for this view.
	void CollectStickyReplays(FName ViewName, TArray<FTSICWebPendingOutbound>& OutReplays) const;

	// Returns a JSON array describing all registered channels (for tsic.describe()).
	FString GetChannelDescriptionJson() const;

	// Dump channels to log (for the WebUI.DumpChannels cheat).
	void DumpToLog() const;

private:
	void EncodeOutbound(FName Channel, const FString& PayloadJson, FName TargetView, TArray<FTSICWebPendingOutbound>& OutQueue) const;

	TWeakObjectPtr<UTSICWebUISubsystem> Owner;

	TMap<FName, FTSICWebChannelInfo> Channels;
	TMap<FName, TArray<TPair<FDelegateHandle, FTSICWebEventHandler>>> Handlers;
	TMap<FName, FTSICWebRequestHandler> RequestHandlers;

	// Thread-safe inbound/outbound queues. Inbound is MPSC (JS-callback thread is the
	// game thread, but we use TQueue for safety against future re-entrancy).
	TQueue<FTSICWebPendingInbound, EQueueMode::Mpsc> InboundQueue;
	TQueue<FTSICWebPendingOutbound, EQueueMode::Mpsc> OutboundQueue;

	mutable FCriticalSection ChannelsCS;
};
