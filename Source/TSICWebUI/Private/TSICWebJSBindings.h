#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include <Ultralight/RefPtr.h>
#include <Ultralight/View.h>
THIRD_PARTY_INCLUDES_END

class UTSICWebUISubsystem;

namespace TSICWebUI::JSBindings
{
	// Sets up `window.tsic` on the given view's current JS context.
	// Call from LoadListener::OnWindowObjectReady. The `OwnerToken` is stored
	// as JSContext private data and is used by the C function callbacks to
	// route inbound JS->C++ messages to the correct subsystem + view.
	void Install(ultralight::View* View, UTSICWebUISubsystem* Subsystem, FName ViewName);

	// Dispatches a C++->JS event (or response) on the given view.
	// Must be called from the same thread that drives Renderer::Update (the game thread).
	// If RequestId >= 0 this resolves/rejects the JS promise; otherwise it fans out via __dispatch.
	// MetaJson, when non-empty, is forwarded to JS as the third argument of the
	// `__dispatch` handler call. Used by the replay cache to tag stale messages.
	void DispatchToView(ultralight::View* View, FName Channel, const FString& PayloadJson, int32 RequestId, const FString& ErrorMessage, const FString& MetaJson = FString());

	// Forget any binding tokens previously registered for the given view name.
	// Called when a view is destroyed so stale entries don't accumulate (and so a
	// later JS callback into a recycled token id can't reach a wrong subsystem).
	void ReleaseTokensForView(FName ViewName);
}
