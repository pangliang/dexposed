/*
 * Original work Copyright (c) 2005-2008, The Android Open Source Project
 * Modified work Copyright (c) 2013, rovo89 and Tungstwenty
 * Modified work Copyright (c) 2015, Alibaba Mobile Infrastructure (Android) Team
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "dexposed.h"

#include <utils/Log.h>
#include <android_runtime/AndroidRuntime.h>
#include <stdio.h>
#include <sys/mman.h>
#include <cutils/properties.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <entrypoints/entrypoint_utils.h>
#include <entrypoints/interpreter/interpreter_entrypoints.h>

#include "quick_argument_visitor.h"
#include "art_method_proxy.h"
#include "ArgArray.h"

namespace art {

    jclass dexposed_class = NULL;
    jmethodID dexposed_handle_hooked_method = NULL;

    int RUNNING_PLATFORM_SDK_VERSION = 0;

    bool dexposedOnVmCreated(JNIEnv *env, const char *) {

        char sdk[PROPERTY_VALUE_MAX];
        const char *error;

        property_get("ro.build.version.sdk", sdk, "0");
        RUNNING_PLATFORM_SDK_VERSION = atoi(sdk);

        LOG(INFO) << "dexposed >>> RUNNING_PLATFORM_SDK_VERSION = " << RUNNING_PLATFORM_SDK_VERSION;

        ArtMethodProxy::ShowOriginalOffset();

        dexposed_class = env->FindClass(DEXPOSED_CLASS);
        dexposed_class = reinterpret_cast<jclass>(env->NewGlobalRef(dexposed_class));

        if (dexposed_class == NULL) {
            LOG(ERROR) << "dexposed: Error while loading Dexposed class " << DEXPOSED_CLASS;
            env->ExceptionClear();
            return false;
        }

        LOG(INFO) << "dexposed: now initializing, Found Dexposed class " << DEXPOSED_CLASS;
        if (register_com_taobao_android_dexposed_DexposedBridge(env) != JNI_OK) {
            LOG(ERROR) << "dexposed: Could not register natives for " << DEXPOSED_CLASS;
            env->ExceptionClear();
            return false;
        }

        jmethodID dexposedbridgeMainMethod = env->GetStaticMethodID(dexposed_class,
                                                                    "main", "()V");
        if (dexposedbridgeMainMethod == NULL) {
            LOG(ERROR) << "dexposed: Could not find method " << DEXPOSED_CLASS << ".main()";
            env->ExceptionClear();
            return false;
        }
        env->CallStaticVoidMethod(dexposed_class, dexposedbridgeMainMethod);

        return true;
    }

    extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *) {
        JNIEnv *env = NULL;
        jint result = -1;

        if (vm->GetEnv((void **) &env, JNI_VERSION_1_6) != JNI_OK) {
            return result;
        }

        int keepLoadingDexposed = dexposedOnVmCreated(env, NULL);

        return JNI_VERSION_1_6;
    }

    static jboolean com_taobao_android_dexposed_DexposedBridge_initNative(JNIEnv *env,
                                                                          jclass) {

        LOG(INFO) << "dexposed: com_taobao_android_dexposed_DexposedBridge_initNative";

        dexposed_handle_hooked_method =
                env->GetStaticMethodID(dexposed_class, "handleHookedMethod",
                                       "(Ljava/lang/reflect/Member;ILjava/lang/Object;Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");
        if (dexposed_handle_hooked_method == NULL) {
            LOG(ERROR) << "dexposed: Could not find method " << DEXPOSED_CLASS <<
            ".handleHookedMethod()";
            env->ExceptionClear();
            return false;
        }
        return true;
    }

    extern "C" void art_quick_dexposed_invoke_handler();

    JValue InvokeXposedHandleHookedMethod(ScopedObjectAccessAlreadyRunnable &soa,
                                          const char *shorty,
                                          jobject rcvr_jobj, ArtMethod* method,
                                          std::vector<jvalue> &args)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {

        LOG(INFO) << "dexposed: InvokeXposedHandleHookedMethod";

        // Build argument array possibly triggering GC.
        soa.Self()->AssertThreadSuspensionIsAllowable();
        jobjectArray args_jobj = nullptr;
        const JValue zero;

        int32_t target_sdk_version = Runtime::Current()->GetTargetSdkVersion();
        // Do not create empty arrays unless needed to maintain Dalvik bug compatibility.
        if (args.size() > 0 || (target_sdk_version > 0 && target_sdk_version <= 22)) {
            args_jobj = soa.Env()->NewObjectArray(args.size(), WellKnownClasses::java_lang_Object,
                                                  nullptr);
            if (args_jobj == NULL) {
                CHECK(soa.Self()->IsExceptionPending());
                return zero;
            }
            mirror::ObjectArray<mirror::Object>* objectArray = soa.Decode<mirror::ObjectArray<mirror::Object> *>(args_jobj);
            for (size_t i = 0; i < args.size(); ++i) {
                JValue jv;
                mirror::Object *val;
                switch(shorty[i + 1]){
                    case 'L':
                        jv.SetL(soa.Decode<Object*>(args[i].l));
                        break;
                    case 'D':
                    case 'J':
                        jv.SetJ(args[i].j);
                        break;
                    default:
                        jv.SetI(args[i].i);
                        break;

                }
                val = BoxPrimitive(Primitive::GetType(shorty[i + 1]), jv);
                objectArray->SetFieldObjectWithoutWriteBarrier<false, false, kVerifyNone,false>(objectArray->OffsetOfElement(i), val);
            }
        }
        ArtMethodProxy artMethodProxy(method,RUNNING_PLATFORM_SDK_VERSION);

        DexposedHookInfo* hookInfo = reinterpret_cast<DexposedHookInfo*>(artMethodProxy.GetFieldPtr(ArtMethodProxy::Member::entry_point_from_jni_));

        // Call XposedBridge.handleHookedMethod(Member method, int originalMethodId, Object additionalInfoObj,
        //                                      Object thisObject, Object[] args)
        jvalue invocation_args[5];
        invocation_args[0].l = hookInfo->reflectedMethod;
        invocation_args[1].i = 0;
        invocation_args[2].l = hookInfo->additionalInfo;
        invocation_args[3].l = rcvr_jobj;
        invocation_args[4].l = args_jobj;
        jobject result =
                soa.Env()->CallStaticObjectMethodA(dexposed_class,
                                                   dexposed_handle_hooked_method,
                                                   invocation_args);
        // Unbox the result if necessary and return it.
        if (UNLIKELY(soa.Self()->IsExceptionPending())) {
            return zero;
        } else {
            if (shorty[0] == 'V' || (shorty[0] == 'L' && result == NULL)) {
                return zero;
            }
            StackHandleScope<1> hs(soa.Self());
            // This can cause thread suspension.
            mirror::Object *rcvr = soa.Decode<mirror::Object *>(rcvr_jobj);
            ThrowLocation throw_location(rcvr, method->GetInterfaceMethodIfProxy(), -1);
            mirror::Object *result_ref = soa.Decode<mirror::Object *>(result);
            mirror::Class *result_type = artMethodProxy.GetReturnType(false);
            JValue result_unboxed;
            if (!UnboxPrimitiveForResult(throw_location, result_ref, result_type,
                                         &result_unboxed)) {
                DCHECK(soa.Self()->IsExceptionPending());
                return zero;
            }
            return result_unboxed;
        }
    }

    // Handler for invocation on proxy methods. On entry a frame will exist for the proxy object method
    // which is responsible for recording callee save registers. We explicitly place into jobjects the
    // incoming reference arguments (so they survive GC). We invoke the invocation handler, which is a
    // field within the proxy object, which will box the primitive arguments and deal with error cases.
    extern "C" uint64_t artQuickDexposedInvokeHandler(ArtMethod *art_method,
                                                      Object *receiver, Thread *self,
                                                      StackReference<ArtMethod> *sp)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {

        const bool is_static = art_method->IsStatic();

        // Ensure we don't get thread suspension until the object arguments are safely in jobjects.
        const char *old_cause = self->StartAssertNoThreadSuspension(
                "Adding to IRT proxy object arguments");

        // Register the top of the managed stack, making stack crawlable.
        DCHECK_EQ(sp->AsMirrorPtr(), art_method) << PrettyMethod(art_method);
        self->SetTopOfStack(sp, 0);

        // Start new JNI local reference state.
        ScopedObjectAccessUnchecked soa(self);
        // Create local ref. copies of proxy method and the receiver.
        jobject rcvr_jobj = is_static ? nullptr : soa.AddLocalReference<jobject>(receiver);

        std::vector<jvalue> args;

        ArtMethodProxy proxy(art_method,RUNNING_PLATFORM_SDK_VERSION);
        uint32_t shorty_len = 0;
        const char *shorty = proxy.GetShorty(&shorty_len);

        BuildQuickArgumentVisitor local_ref_visitor(sp, is_static, shorty, shorty_len, &soa, &args);
        local_ref_visitor.VisitArguments();
        if (!is_static) {
            DCHECK_GT(args.size(), 0U) << PrettyMethod(art_method);
            args.erase(args.begin());
        }
        LOG(INFO) << "dexposed: artQuickDexposedInvokeHandler " << "GetDexMethodIndex=" << proxy.GetDexMethodIndex() << " shorty=" << shorty << " args.size=" << args.size();

        self->EndAssertNoThreadSuspension(old_cause);
        JValue result = InvokeXposedHandleHookedMethod(soa, shorty, rcvr_jobj, art_method,args);
        local_ref_visitor.FixupReferences();
        return result.GetJ();
    }

    static void com_taobao_android_dexposed_DexposedBridge_hookMethodNative(
            JNIEnv *env, jclass, jobject java_method, jobject, jint jSlot,
            jobject additional_info) {
        ScopedObjectAccess soa(env);

        mirror::ArtField* f =
                soa.DecodeField(WellKnownClasses::java_lang_reflect_AbstractMethod_artMethod);

        mirror::ArtMethod* method = f->GetObject(soa.Decode<mirror::Object*>(java_method))->AsArtMethod();

        ArtMethod* art_method = ArtMethod::FromReflectedMethod(soa, java_method);
        ArtMethodProxy art_method_proxy(art_method,RUNNING_PLATFORM_SDK_VERSION);

        LOG(INFO) << " dexposed: >>> hookMethodNative " << art_method << " " << PrettyMethod(art_method);

        if (dexposedIsHooked(art_method)) {
            LOG(INFO) << "dexposed: >>> Already hooked " << art_method << " " << PrettyMethod(art_method);
            return;
        }

        // Create a backup of the ArtMethod object
        ArtMethod *backup_method = reinterpret_cast<ArtMethod *>(art_method->Clone(soa.Self()));
        // Set private flag to avoid virtual table lookups during invocation
        backup_method->SetAccessFlags(
                backup_method->GetAccessFlags() /*| kAccXposedOriginalMethod*/);
        // Create a Method/Constructor object for the backup ArtMethod object
        jobject reflect_method;
        if (art_method->IsConstructor()) {
            reflect_method = env->AllocObject(WellKnownClasses::java_lang_reflect_Constructor);
        } else {
            reflect_method = env->AllocObject(WellKnownClasses::java_lang_reflect_Method);
        }
        env->SetObjectField(reflect_method,
                            WellKnownClasses::java_lang_reflect_AbstractMethod_artMethod,
                            env->NewGlobalRef(soa.AddLocalReference<jobject>(backup_method)));
        // Save extra information in a separate structure, stored instead of the native method
        DexposedHookInfo *hookInfo = reinterpret_cast<DexposedHookInfo *>(calloc(1,
                                                                                 sizeof(DexposedHookInfo)));
        hookInfo->reflectedMethod = env->NewGlobalRef(reflect_method);
        hookInfo->additionalInfo = env->NewGlobalRef(additional_info);
        hookInfo->originalMethod = backup_method;

        art_method_proxy.SetFieldPtr(ArtMethodProxy::Member::entry_point_from_jni_, reinterpret_cast<void *>(hookInfo));

        art_method_proxy.SetFieldPtr(ArtMethodProxy::Member::entry_point_from_quick_comiled_code_,reinterpret_cast<void *>(art_quick_dexposed_invoke_handler));


    }

    static bool dexposedIsHooked(ArtMethod *art_method) {
        ArtMethodProxy art_method_proxy(art_method,RUNNING_PLATFORM_SDK_VERSION);
        return art_method_proxy.GetFieldPtr(ArtMethodProxy::Member::entry_point_from_quick_comiled_code_)
               == (void *) art_quick_dexposed_invoke_handler;
    }

    extern "C" jobject com_taobao_android_dexposed_DexposedBridge_invokeOriginalMethodNative(
            JNIEnv *env, jclass, jobject java_method, jint, jobject, jobject,
            jobject thiz, jobject args)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {

        ScopedObjectAccess soa(env);
        ArtMethod *art_method = ArtMethod::FromReflectedMethod(soa, java_method);

        LOG(INFO) << "dexposed: >>> invokeOriginalMethodNative " << PrettyMethod(art_method);

        if(dexposedIsHooked(art_method))
        {
            LOG(ERROR) << "dexposed: >>> invokeOriginalMethodNative dexposedIsHooked ";
            return nullptr;
        }

        ArtMethodProxy proxy(art_method,RUNNING_PLATFORM_SDK_VERSION);

        Object* receiver = proxy.IsStatic() ? NULL : soa.Decode<Object*>(thiz);

        uint32_t shorty_len;
        const char* shorty = proxy.GetShorty(&shorty_len);

        LOG(INFO) << "dexposed: >>> invokeOriginalMethodNative shorty:" << shorty;


        mirror::ObjectArray<mirror::Object>* objectArray = soa.Decode<mirror::ObjectArray<mirror::Object> *>(args);
        ArgArray arg_array (shorty, shorty_len);

        if (!arg_array.BuildArgArrayFromObjectArray(soa,receiver,objectArray, proxy, RUNNING_PLATFORM_SDK_VERSION)) {
            CHECK(soa.Self()->IsExceptionPending());
            return nullptr;
        }

        JValue result ;
        art_method->Invoke(soa.Self(), arg_array.GetArray() , arg_array.GetNumBytes(), &result, shorty);

        if (soa.Self()->IsExceptionPending()) {
            jthrowable th = soa.Env()->ExceptionOccurred();
            soa.Env()->ExceptionClear();
            jclass exception_class = soa.Env()->FindClass("java/lang/reflect/InvocationTargetException");
            jmethodID mid = soa.Env()->GetMethodID(exception_class, "<init>", "(Ljava/lang/Throwable;)V");
            jobject exception_instance = soa.Env()->NewObject(exception_class, mid, th);
            soa.Env()->Throw(reinterpret_cast<jthrowable>(exception_instance));
            return nullptr;
        }

        // Box if necessary and return.
        return soa.AddLocalReference<jobject>(BoxPrimitive(Primitive::GetType(shorty[0]),
                                                           result));
    }

    extern "C" jobject com_taobao_android_dexposed_DexposedBridge_invokeSuperNative(
            JNIEnv *env, jclass, jobject thiz, jobject args, jobject java_method, jobject, jobject,
            jint slot, jboolean check)

    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {

        LOG(INFO) << "dexposed: >>> invokeSuperNative";

        ScopedObjectAccess soa(env);
        ArtMethod *method = ArtMethod::FromReflectedMethod(soa, java_method);

        // Find the actual implementation of the virtual method.
        ArtMethod *art_method = method->FindOverriddenMethod();

        Object* receiver = art_method->IsStatic() ? NULL : soa.Decode<Object*>(thiz);
        StackHandleScope<1> hs(soa.Self());
        MethodHelper mh(hs.NewHandle(art_method));
        mirror::ObjectArray<mirror::Object>* objectArray = soa.Decode<mirror::ObjectArray<mirror::Object> *>(args);
        ArgArray arg_array(mh.GetShorty(), mh.GetShortyLength());
        arg_array.BuildArgArrayFromObjectArray(soa,receiver,objectArray,mh);

        JValue result ;
        art_method->Invoke(soa.Self(), arg_array.GetArray() , arg_array.GetNumBytes(), &result, mh.GetShorty());
        if(mh.GetShorty()[0] == 'V')
            return nullptr;
        else if(mh.GetShorty()[0] == 'L')
            return soa.AddLocalReference<jobject>(result.GetL());
        else
            return soa.AddLocalReference<jobject>(BoxPrimitive(Primitive::GetType(mh.GetShorty()[0]), result));
    }

    static const JNINativeMethod dexposedMethods[] =
            {
                    {"initNative",        "()Z", (void *) com_taobao_android_dexposed_DexposedBridge_initNative},
                    {"hookMethodNative",  "(Ljava/lang/reflect/Member;Ljava/lang/Class;ILjava/lang/Object;)V",
                                                 (void *) com_taobao_android_dexposed_DexposedBridge_hookMethodNative},
                    {"invokeOriginalMethodNative",
                                          "(Ljava/lang/reflect/Member;I[Ljava/lang/Class;Ljava/lang/Class;Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;",
                                                 (void *) com_taobao_android_dexposed_DexposedBridge_invokeOriginalMethodNative},
                    {"invokeSuperNative", "(Ljava/lang/Object;[Ljava/lang/Object;Ljava/lang/reflect/Member;Ljava/lang/Class;[Ljava/lang/Class;Ljava/lang/Class;I)Ljava/lang/Object;",
                                                 (void *) com_taobao_android_dexposed_DexposedBridge_invokeSuperNative},
            };

    static int register_com_taobao_android_dexposed_DexposedBridge(JNIEnv *env) {
        return env->RegisterNatives(dexposed_class, dexposedMethods,
                                    sizeof(dexposedMethods) / sizeof(dexposedMethods[0]));
    }
}

