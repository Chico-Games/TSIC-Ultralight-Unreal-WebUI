#include "TSICWebJSBindings.h"
#include "TSICWebEventBus.h"
#include "TSICWebStringHelpers.h"
#include "TSICWebUI.h"
#include "TSICWebUISubsystem.h"

THIRD_PARTY_INCLUDES_START
#include <Ultralight/JavaScript.h>
#include <JavaScriptCore/JSStringRef.h>
#include <JavaScriptCore/JSObjectRef.h>
#include <JavaScriptCore/JSValueRef.h>
THIRD_PARTY_INCLUDES_END

namespace TSICWebUI::JSBindings
{
	namespace
	{
		// Per-view binding token kept alive in the C++ side and looked up by JS callbacks
		// via the JSContext's globalContext's `__tsicBindingToken` numeric property.
		struct FBindingToken
		{
			TWeakObjectPtr<UTSICWebUISubsystem> Subsystem;
			FName ViewName;
		};

		TMap<int32, TUniquePtr<FBindingToken>> GBindingTokens;
		int32 GNextTokenId = 1;
		FCriticalSection GBindingTokensCS;

		int32 RegisterToken(UTSICWebUISubsystem* Subsystem, FName ViewName)
		{
			FScopeLock Lock(&GBindingTokensCS);
			const int32 Id = GNextTokenId++;
			TUniquePtr<FBindingToken> Token = MakeUnique<FBindingToken>();
			Token->Subsystem = Subsystem;
			Token->ViewName = ViewName;
			GBindingTokens.Add(Id, MoveTemp(Token));
			return Id;
		}

		FBindingToken* LookupToken(int32 Id)
		{
			FScopeLock Lock(&GBindingTokensCS);
			TUniquePtr<FBindingToken>* Found = GBindingTokens.Find(Id);
			return (Found && Found->IsValid()) ? Found->Get() : nullptr;
		}

		void ReleaseTokensForViewInternal(FName ViewName)
		{
			FScopeLock Lock(&GBindingTokensCS);
			for (auto It = GBindingTokens.CreateIterator(); It; ++It)
			{
				if (It->Value.IsValid() && It->Value->ViewName == ViewName)
				{
					It.RemoveCurrent();
				}
			}
		}

		FString JSStringToFString(JSStringRef Str)
		{
			if (!Str)
			{
				return FString();
			}
			const size_t Max = JSStringGetMaximumUTF8CStringSize(Str);
			TArray<char> Buf;
			Buf.SetNumUninitialized(static_cast<int32>(Max));
			const size_t Written = JSStringGetUTF8CString(Str, Buf.GetData(), Max);
			if (Written == 0)
			{
				return FString();
			}
			return FString(UTF8_TO_TCHAR(Buf.GetData()));
		}

		FString JSValueToFString(JSContextRef Ctx, JSValueRef Value)
		{
			if (!Value || JSValueIsUndefined(Ctx, Value) || JSValueIsNull(Ctx, Value))
			{
				return TEXT("null");
			}
			JSStringRef Str = JSValueToStringCopy(Ctx, Value, nullptr);
			FString Out = JSStringToFString(Str);
			JSStringRelease(Str);
			return Out;
		}

		int32 GetBindingTokenFromContext(JSContextRef Ctx)
		{
			JSObjectRef Global = JSContextGetGlobalObject(Ctx);
			JSStringRef PropName = JSStringCreateWithUTF8CString("__tsicBindingToken");
			JSValueRef Val = JSObjectGetProperty(Ctx, Global, PropName, nullptr);
			JSStringRelease(PropName);
			if (!Val || !JSValueIsNumber(Ctx, Val))
			{
				return 0;
			}
			return static_cast<int32>(JSValueToNumber(Ctx, Val, nullptr));
		}

		JSValueRef SendCallback(JSContextRef Ctx, JSObjectRef /*Func*/, JSObjectRef /*This*/, size_t ArgCount, const JSValueRef Args[], JSValueRef* /*Exception*/)
		{
			if (ArgCount < 2)
			{
				return JSValueMakeUndefined(Ctx);
			}
			const FString ChannelStr = JSValueToFString(Ctx, Args[0]);
			const FString PayloadJson = JSValueToFString(Ctx, Args[1]);
			const int32 TokenId = GetBindingTokenFromContext(Ctx);
			FBindingToken* Token = LookupToken(TokenId);
			if (!Token || !Token->Subsystem.IsValid())
			{
				return JSValueMakeUndefined(Ctx);
			}
			Token->Subsystem->HandleJSSend(FName(*ChannelStr), PayloadJson, Token->ViewName);
			return JSValueMakeUndefined(Ctx);
		}

		JSValueRef RequestCallback(JSContextRef Ctx, JSObjectRef /*Func*/, JSObjectRef /*This*/, size_t ArgCount, const JSValueRef Args[], JSValueRef* /*Exception*/)
		{
			if (ArgCount < 3)
			{
				return JSValueMakeUndefined(Ctx);
			}
			const FString ChannelStr = JSValueToFString(Ctx, Args[0]);
			const FString PayloadJson = JSValueToFString(Ctx, Args[1]);
			const int32 ReqId = static_cast<int32>(JSValueToNumber(Ctx, Args[2], nullptr));
			const int32 TokenId = GetBindingTokenFromContext(Ctx);
			FBindingToken* Token = LookupToken(TokenId);
			if (!Token || !Token->Subsystem.IsValid())
			{
				return JSValueMakeUndefined(Ctx);
			}
			Token->Subsystem->HandleJSRequest(FName(*ChannelStr), PayloadJson, ReqId, Token->ViewName);
			return JSValueMakeUndefined(Ctx);
		}

		JSValueRef DescribeCallback(JSContextRef Ctx, JSObjectRef /*Func*/, JSObjectRef /*This*/, size_t /*ArgCount*/, const JSValueRef /*Args*/[], JSValueRef* /*Exception*/)
		{
			const int32 TokenId = GetBindingTokenFromContext(Ctx);
			FBindingToken* Token = LookupToken(TokenId);
			if (!Token || !Token->Subsystem.IsValid())
			{
				return JSValueMakeString(Ctx, JSStringCreateWithUTF8CString("[]"));
			}
			const FString Json = Token->Subsystem->GetEventBusChannelsJson();
			const FTCHARToUTF8 Conv(*Json);
			JSStringRef Str = JSStringCreateWithUTF8CString(Conv.Get());
			JSValueRef Out = JSValueMakeString(Ctx, Str);
			JSStringRelease(Str);
			return Out;
		}

		JSValueRef DescribeMessagesCallback(JSContextRef Ctx, JSObjectRef /*Func*/, JSObjectRef /*This*/, size_t /*ArgCount*/, const JSValueRef /*Args*/[], JSValueRef* /*Exception*/)
		{
			const int32 TokenId = GetBindingTokenFromContext(Ctx);
			FBindingToken* Token = LookupToken(TokenId);
			if (!Token || !Token->Subsystem.IsValid())
			{
				return JSValueMakeString(Ctx, JSStringCreateWithUTF8CString("[]"));
			}
			const FString Json = Token->Subsystem->GetMessageBridgeDescriptionJson();
			const FTCHARToUTF8 Conv(*Json);
			JSStringRef Str = JSStringCreateWithUTF8CString(Conv.Get());
			JSValueRef Out = JSValueMakeString(Ctx, Str);
			JSStringRelease(Str);
			return Out;
		}

		JSValueRef PublishMessageCallback(JSContextRef Ctx, JSObjectRef /*Func*/, JSObjectRef /*This*/, size_t ArgCount, const JSValueRef Args[], JSValueRef* /*Exception*/)
		{
			if (ArgCount < 2)
			{
				return JSValueMakeBoolean(Ctx, false);
			}
			const FString TagStr = JSValueToFString(Ctx, Args[0]);
			const FString PayloadJson = JSValueToFString(Ctx, Args[1]);
			const int32 TokenId = GetBindingTokenFromContext(Ctx);
			FBindingToken* Token = LookupToken(TokenId);
			if (!Token || !Token->Subsystem.IsValid())
			{
				return JSValueMakeBoolean(Ctx, false);
			}
			const bool bOk = Token->Subsystem->PublishGameplayMessageFromJson(FName(*TagStr), PayloadJson);
			return JSValueMakeBoolean(Ctx, bOk);
		}

		JSValueRef SetInteractiveRectsCallback(JSContextRef Ctx, JSObjectRef /*Func*/, JSObjectRef /*This*/, size_t ArgCount, const JSValueRef Args[], JSValueRef* /*Exception*/)
		{
			if (ArgCount < 1)
			{
				return JSValueMakeUndefined(Ctx);
			}
			const FString RectsJson = JSValueToFString(Ctx, Args[0]);
			const int32 TokenId = GetBindingTokenFromContext(Ctx);
			FBindingToken* Token = LookupToken(TokenId);
			if (!Token || !Token->Subsystem.IsValid())
			{
				return JSValueMakeUndefined(Ctx);
			}
			Token->Subsystem->SetInteractiveRectsFromJson(Token->ViewName, RectsJson);
			return JSValueMakeUndefined(Ctx);
		}

		void SetNativeFunction(JSContextRef Ctx, JSObjectRef Parent, const char* Name, JSObjectCallAsFunctionCallback Fn)
		{
			JSStringRef NameRef = JSStringCreateWithUTF8CString(Name);
			JSObjectRef Func = JSObjectMakeFunctionWithCallback(Ctx, NameRef, Fn);
			JSObjectSetProperty(Ctx, Parent, NameRef, Func, kJSPropertyAttributeDontEnum, nullptr);
			JSStringRelease(NameRef);
		}

		// JS-side support code that wraps the natives in a friendly window.tsic surface.
		const char* kTSICBootstrap =
			"(function() {\n"
			"  window.tsic = {\n"
			"    _subs: {},\n"
			"    _pending: {},\n"
			"    _nextReqId: 1,\n"
			"    _lastSticky: {},\n"
			"    send: function(name, payload) {\n"
			"      __tsicSend(name, JSON.stringify(payload === undefined ? null : payload));\n"
			"    },\n"
			"    request: function(name, payload) {\n"
			"      var reqId = this._nextReqId++;\n"
			"      var self = this;\n"
			"      return new Promise(function(resolve, reject) {\n"
			"        self._pending[reqId] = [resolve, reject];\n"
			"        __tsicRequest(name, JSON.stringify(payload === undefined ? null : payload), reqId);\n"
			"      });\n"
			"    },\n"
			"    on: function(name, cb) {\n"
			"      (this._subs[name] = this._subs[name] || []).push(cb);\n"
			"      var cached = this._lastSticky[name];\n"
			"      if (cached) {\n"
			"        try { cb(cached.payload, cached.meta, name); } catch (e) { console.error(e); }\n"
			"      }\n"
			"    },\n"
			"    off: function(name, cb) {\n"
			"      var arr = this._subs[name]; if (!arr) return;\n"
			"      var i = arr.indexOf(cb); if (i >= 0) arr.splice(i, 1);\n"
			"    },\n"
			"    describe: function() {\n"
			"      try { return JSON.parse(__tsicDescribe()); } catch (e) { return []; }\n"
			"    },\n"
			"    describeMessages: function() {\n"
			"      try { return JSON.parse(__tsicDescribeMessages()); } catch (e) { return []; }\n"
			"    },\n"
			"    publishMessage: function(tag, payload) {\n"
			"      return __tsicPublishMessage(tag, JSON.stringify(payload === undefined ? {} : payload));\n"
			"    },\n"
			"    setInteractiveRects: function(rects) {\n"
			"      __tsicSetInteractiveRects(JSON.stringify(rects || []));\n"
			"    },\n"
			"    __dispatch: function(name, payloadJson, metaJson) {\n"
			"      var payload;\n"
			"      try { payload = JSON.parse(payloadJson); } catch (e) { payload = payloadJson; }\n"
			"      var meta = undefined;\n"
			"      if (metaJson) { try { meta = JSON.parse(metaJson); } catch (e) {} }\n"
			"      if (meta && meta.cachedAt) {\n"
			"        this._lastSticky[name] = { payload: payload, meta: meta };\n"
			"      }\n"
			"      var subs = this._subs[name]; if (!subs) return;\n"
			"      for (var i = 0; i < subs.length; i++) {\n"
			"        try { subs[i](payload, meta, name); } catch (e) { console.error(e); }\n"
			"      }\n"
			"    },\n"
			"    __resolveResponse: function(reqId, payloadJson, errorMsg) {\n"
			"      var p = this._pending[reqId]; if (!p) return;\n"
			"      delete this._pending[reqId];\n"
			"      if (errorMsg) { p[1](new Error(errorMsg)); return; }\n"
			"      var payload;\n"
			"      try { payload = JSON.parse(payloadJson); } catch (e) { payload = payloadJson; }\n"
			"      p[0](payload);\n"
			"    }\n"
			"  };\n"
			"})();\n";
	}

	void Install(ultralight::View* View, UTSICWebUISubsystem* Subsystem, FName ViewName)
	{
		if (!View || !Subsystem)
		{
			return;
		}

		ultralight::RefPtr<ultralight::JSContext> CtxRef = View->LockJSContext();
		JSContextRef Ctx = CtxRef->ctx();
		JSObjectRef Global = JSContextGetGlobalObject(Ctx);

		const int32 TokenId = RegisterToken(Subsystem, ViewName);

		// Stamp the token id on the global so callbacks can route back without per-function private data.
		JSStringRef TokenName = JSStringCreateWithUTF8CString("__tsicBindingToken");
		JSObjectSetProperty(Ctx, Global, TokenName, JSValueMakeNumber(Ctx, TokenId),
			kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontEnum | kJSPropertyAttributeDontDelete, nullptr);
		JSStringRelease(TokenName);

		SetNativeFunction(Ctx, Global, "__tsicSend", &SendCallback);
		SetNativeFunction(Ctx, Global, "__tsicRequest", &RequestCallback);
		SetNativeFunction(Ctx, Global, "__tsicDescribe", &DescribeCallback);
		SetNativeFunction(Ctx, Global, "__tsicDescribeMessages", &DescribeMessagesCallback);
		SetNativeFunction(Ctx, Global, "__tsicPublishMessage", &PublishMessageCallback);
		SetNativeFunction(Ctx, Global, "__tsicSetInteractiveRects", &SetInteractiveRectsCallback);

		// Now evaluate the JS bootstrap that wraps the natives into the friendly API.
		JSStringRef Script = JSStringCreateWithUTF8CString(kTSICBootstrap);
		JSEvaluateScript(Ctx, Script, nullptr, nullptr, 1, nullptr);
		JSStringRelease(Script);
	}

	void ReleaseTokensForView(FName ViewName)
	{
		ReleaseTokensForViewInternal(ViewName);
	}

	void DispatchToView(ultralight::View* View, FName Channel, const FString& PayloadJson, int32 RequestId, const FString& ErrorMessage, const FString& MetaJson)
	{
		if (!View)
		{
			return;
		}

		ultralight::RefPtr<ultralight::JSContext> CtxRef = View->LockJSContext();
		JSContextRef Ctx = CtxRef->ctx();
		JSObjectRef Global = JSContextGetGlobalObject(Ctx);

		// Look up `window.tsic`.
		JSStringRef TsicName = JSStringCreateWithUTF8CString("tsic");
		JSValueRef TsicVal = JSObjectGetProperty(Ctx, Global, TsicName, nullptr);
		JSStringRelease(TsicName);
		if (!TsicVal || !JSValueIsObject(Ctx, TsicVal))
		{
			return;
		}
		JSObjectRef TsicObj = JSValueToObject(Ctx, TsicVal, nullptr);
		if (!TsicObj)
		{
			return;
		}

		auto MakeStrArg = [&](const FString& Str)
		{
			const FTCHARToUTF8 Conv(*Str);
			JSStringRef S = JSStringCreateWithUTF8CString(Conv.Get());
			JSValueRef V = JSValueMakeString(Ctx, S);
			JSStringRelease(S);
			return V;
		};

		if (RequestId >= 0)
		{
			JSStringRef MethodName = JSStringCreateWithUTF8CString("__resolveResponse");
			JSValueRef MethodVal = JSObjectGetProperty(Ctx, TsicObj, MethodName, nullptr);
			JSStringRelease(MethodName);
			if (!MethodVal || !JSValueIsObject(Ctx, MethodVal))
			{
				return;
			}
			JSObjectRef MethodObj = JSValueToObject(Ctx, MethodVal, nullptr);

			JSValueRef ErrorArg = ErrorMessage.IsEmpty()
				? JSValueMakeNull(Ctx)
				: MakeStrArg(ErrorMessage);

			const JSValueRef Args[] = {
				JSValueMakeNumber(Ctx, RequestId),
				MakeStrArg(PayloadJson),
				ErrorArg,
			};
			JSObjectCallAsFunction(Ctx, MethodObj, TsicObj, 3, Args, nullptr);
		}
		else
		{
			JSStringRef MethodName = JSStringCreateWithUTF8CString("__dispatch");
			JSValueRef MethodVal = JSObjectGetProperty(Ctx, TsicObj, MethodName, nullptr);
			JSStringRelease(MethodName);
			if (!MethodVal || !JSValueIsObject(Ctx, MethodVal))
			{
				return;
			}
			JSObjectRef MethodObj = JSValueToObject(Ctx, MethodVal, nullptr);

			const JSValueRef Args[] = {
				MakeStrArg(Channel.ToString()),
				MakeStrArg(PayloadJson),
				MetaJson.IsEmpty() ? JSValueMakeNull(Ctx) : MakeStrArg(MetaJson),
			};
			JSObjectCallAsFunction(Ctx, MethodObj, TsicObj, 3, Args, nullptr);
		}
	}
}
