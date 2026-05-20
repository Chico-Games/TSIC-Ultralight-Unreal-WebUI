#include "TSICWebEventBus.h"
#include "TSICWebUI.h"
#include "TSICWebUISubsystem.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	bool IsValidChannelName(FName Name)
	{
		if (Name.IsNone())
		{
			return false;
		}
		const FString Str = Name.ToString();
		if (Str.IsEmpty() || Str.Contains(TEXT(" ")))
		{
			return false;
		}
		return true;
	}
}

FTSICWebEventBus::FTSICWebEventBus(UTSICWebUISubsystem* InOwner)
	: Owner(InOwner)
{
}

FTSICWebEventBus::~FTSICWebEventBus() = default;

void FTSICWebEventBus::RegisterChannel(FName ChannelName, EWebChannelKind Kind, const FString& Description)
{
	if (!IsValidChannelName(ChannelName))
	{
		UE_LOG(LogTSICWebUI, Warning, TEXT("WebEventBus: rejected invalid channel name '%s'."), *ChannelName.ToString());
		return;
	}

	FScopeLock Lock(&ChannelsCS);
	FTSICWebChannelInfo& Info = Channels.FindOrAdd(ChannelName);
	if (Info.Name.IsNone())
	{
		Info.Name = ChannelName;
		Info.Kind = Kind;
		Info.Description = Description;
		UE_LOG(LogTSICWebUI, Verbose, TEXT("WebEventBus: registered channel '%s' (%s)"),
			*ChannelName.ToString(),
			Kind == EWebChannelKind::Sticky ? TEXT("sticky") : TEXT("event"));
	}
}

FDelegateHandle FTSICWebEventBus::RegisterHandler(FName ChannelName, FTSICWebEventHandler Handler)
{
	FScopeLock Lock(&ChannelsCS);
	TArray<TPair<FDelegateHandle, FTSICWebEventHandler>>& List = Handlers.FindOrAdd(ChannelName);
	const FDelegateHandle Handle = Handler.GetHandle();
	List.Add({Handle, MoveTemp(Handler)});
	return Handle;
}

void FTSICWebEventBus::UnregisterHandler(FName ChannelName, FDelegateHandle Handle)
{
	FScopeLock Lock(&ChannelsCS);
	if (TArray<TPair<FDelegateHandle, FTSICWebEventHandler>>* List = Handlers.Find(ChannelName))
	{
		List->RemoveAll([Handle](const TPair<FDelegateHandle, FTSICWebEventHandler>& P)
		{
			return P.Key == Handle;
		});
	}
}

void FTSICWebEventBus::RegisterRequestHandler(FName ChannelName, FTSICWebRequestHandler Handler)
{
	FScopeLock Lock(&ChannelsCS);
	RequestHandlers.Add(ChannelName, MoveTemp(Handler));
}

void FTSICWebEventBus::UnregisterRequestHandler(FName ChannelName)
{
	FScopeLock Lock(&ChannelsCS);
	RequestHandlers.Remove(ChannelName);
}

void FTSICWebEventBus::BroadcastEvent(FName ChannelName, const FString& PayloadJson)
{
	{
		FScopeLock Lock(&ChannelsCS);
		FTSICWebChannelInfo* Info = Channels.Find(ChannelName);
		if (!Info)
		{
			UE_LOG(LogTSICWebUI, Warning, TEXT("WebEventBus: broadcast to unregistered channel '%s' (call RegisterChannel first)."), *ChannelName.ToString());
			return;
		}
		if (Info->Kind == EWebChannelKind::Sticky)
		{
			Info->LastValueJson = PayloadJson;
			Info->bHasLastValue = true;
		}
	}

	FTSICWebPendingOutbound Out;
	Out.Channel = ChannelName;
	Out.PayloadJson = PayloadJson;
	Out.TargetView = NAME_None;
	OutboundQueue.Enqueue(MoveTemp(Out));
}

void FTSICWebEventBus::EnqueueInboundSend(FName ChannelName, const FString& PayloadJson, FName SourceView)
{
	FTSICWebPendingInbound In;
	In.Channel = ChannelName;
	In.PayloadJson = PayloadJson;
	In.RequestId = -1;
	In.SourceView = SourceView;
	InboundQueue.Enqueue(MoveTemp(In));
}

void FTSICWebEventBus::EnqueueInboundRequest(FName ChannelName, const FString& PayloadJson, int32 RequestId, FName SourceView)
{
	FTSICWebPendingInbound In;
	In.Channel = ChannelName;
	In.PayloadJson = PayloadJson;
	In.RequestId = RequestId;
	In.SourceView = SourceView;
	InboundQueue.Enqueue(MoveTemp(In));
}

void FTSICWebEventBus::DrainPending(TArray<FTSICWebPendingOutbound>& OutOutbound)
{
	check(IsInGameThread());

	FTSICWebPendingInbound In;
	while (InboundQueue.Dequeue(In))
	{
		if (In.RequestId < 0)
		{
			// Fire-and-forget send: invoke all C++ handlers, plus broadcast to subscribers.
			TArray<FTSICWebEventHandler> Snapshot;
			{
				FScopeLock Lock(&ChannelsCS);
				if (TArray<TPair<FDelegateHandle, FTSICWebEventHandler>>* List = Handlers.Find(In.Channel))
				{
					Snapshot.Reserve(List->Num());
					for (const TPair<FDelegateHandle, FTSICWebEventHandler>& P : *List)
					{
						Snapshot.Add(P.Value);
					}
				}
			}
			for (FTSICWebEventHandler& H : Snapshot)
			{
				H.ExecuteIfBound(In.Channel, In.PayloadJson);
			}
		}
		else
		{
			// Request: find request handler, invoke, queue response back to source view.
			FTSICWebRequestHandler Handler;
			bool bHasHandler = false;
			{
				FScopeLock Lock(&ChannelsCS);
				if (FTSICWebRequestHandler* Found = RequestHandlers.Find(In.Channel))
				{
					Handler = *Found;
					bHasHandler = true;
				}
			}

			FTSICWebPendingOutbound Response;
			Response.Channel = In.Channel;
			Response.RequestId = In.RequestId;
			Response.TargetView = In.SourceView;

			if (!bHasHandler)
			{
				Response.ErrorMessage = FString::Printf(TEXT("No request handler registered for channel '%s'"), *In.Channel.ToString());
			}
			else
			{
				TOptional<FString> Result = Handler.Execute(In.Channel, In.PayloadJson);
				if (Result.IsSet())
				{
					Response.PayloadJson = MoveTemp(Result.GetValue());
				}
				else
				{
					Response.ErrorMessage = FString::Printf(TEXT("Request handler for '%s' returned no value"), *In.Channel.ToString());
				}
			}
			OutboundQueue.Enqueue(MoveTemp(Response));
		}
	}

	FTSICWebPendingOutbound Out;
	while (OutboundQueue.Dequeue(Out))
	{
		OutOutbound.Add(MoveTemp(Out));
	}
}

void FTSICWebEventBus::CollectStickyReplays(FName ViewName, TArray<FTSICWebPendingOutbound>& OutReplays) const
{
	FScopeLock Lock(&ChannelsCS);
	for (const TPair<FName, FTSICWebChannelInfo>& Pair : Channels)
	{
		const FTSICWebChannelInfo& Info = Pair.Value;
		if (Info.Kind != EWebChannelKind::Sticky || !Info.bHasLastValue)
		{
			continue;
		}
		FTSICWebPendingOutbound Replay;
		Replay.Channel = Info.Name;
		Replay.PayloadJson = Info.LastValueJson;
		Replay.TargetView = ViewName;
		OutReplays.Add(MoveTemp(Replay));
	}
}

FString FTSICWebEventBus::GetChannelDescriptionJson() const
{
	TArray<TSharedPtr<FJsonValue>> Items;
	{
		FScopeLock Lock(&ChannelsCS);
		for (const TPair<FName, FTSICWebChannelInfo>& Pair : Channels)
		{
			const FTSICWebChannelInfo& Info = Pair.Value;
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("name"), Info.Name.ToString());
			Obj->SetStringField(TEXT("kind"), Info.Kind == EWebChannelKind::Sticky ? TEXT("sticky") : TEXT("event"));
			Obj->SetStringField(TEXT("description"), Info.Description);
			Items.Add(MakeShared<FJsonValueObject>(Obj));
		}
	}
	FString Out;
	TSharedRef<TJsonWriter<TCHAR>> Writer = TJsonWriterFactory<TCHAR>::Create(&Out);
	FJsonSerializer::Serialize(Items, Writer);
	return Out;
}

void FTSICWebEventBus::DumpToLog() const
{
	FScopeLock Lock(&ChannelsCS);
	UE_LOG(LogTSICWebUI, Display, TEXT("=== TSIC Web UI channels (%d) ==="), Channels.Num());
	for (const TPair<FName, FTSICWebChannelInfo>& Pair : Channels)
	{
		const FTSICWebChannelInfo& Info = Pair.Value;
		const int32 NumHandlers = Handlers.Contains(Info.Name) ? Handlers[Info.Name].Num() : 0;
		const bool bHasReq = RequestHandlers.Contains(Info.Name);
		UE_LOG(LogTSICWebUI, Display, TEXT("  %s [%s] handlers=%d req=%s sticky=%s -- %s"),
			*Info.Name.ToString(),
			Info.Kind == EWebChannelKind::Sticky ? TEXT("sticky") : TEXT("event"),
			NumHandlers,
			bHasReq ? TEXT("yes") : TEXT("no"),
			Info.bHasLastValue ? TEXT("yes") : TEXT("no"),
			*Info.Description);
	}
}
