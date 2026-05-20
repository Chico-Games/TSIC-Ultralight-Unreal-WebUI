#include "TSICWebMessageBridge.h"

namespace TSICWebUI
{
	TArray<FString> CollectStructFieldNames(const UScriptStruct* Struct)
	{
		TArray<FString> Out;
		if (!Struct)
		{
			return Out;
		}
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Prop = *It;
			if (!Prop || Prop->HasAnyPropertyFlags(CPF_Transient))
			{
				continue;
			}
			Out.Add(Prop->GetAuthoredName());
		}
		return Out;
	}

	FName MakeMessageChannelName(FGameplayTag Tag)
	{
		return FName(*FString::Printf(TEXT("tsic.msg.%s"), *Tag.ToString()));
	}
}
