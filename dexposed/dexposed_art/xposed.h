#ifndef XPOSED_H
#define XPOSED_H

#define ANDROID_SMP 0
#include <jni.h>
#include <mirror/art_method.h>
#include <mirror/object.h>
#include <mirror/array.h>
#include <mirror/class.h>
#include <well_known_classes.h>
#include <class_linker.h>
#include <primitive.h>
#include <reflection.h>
#include <runtime.h>
#include <scoped_thread_state_change.h>
#include <method_helper.h>
#include <method_helper-inl.h>
#include <handle.h>
#include <util.h>
#include <throw_location.h>
#include <stack.h>
#include <jni_internal.h>

using art::mirror::ArtMethod;
using art::mirror::Array;
using art::mirror::ObjectArray;
using art::mirror::Class;
using art::mirror::Object;
using art::PrettyMethod;
using art::Runtime;

using art::WellKnownClasses;
using art::Primitive;
using art::BoxPrimitive;
using art::UnboxPrimitiveForResult;
using art::UnboxPrimitiveForField;
using art::JValue;
using art::ScopedObjectAccess;
using art::ScopedObjectAccessUnchecked;
using art::ScopedObjectAccessAlreadyRunnable;
using art::StackHandleScope;
using art::Handle;
using art::MethodHelper;
using art::StackReference;
using art::ClassLinker;
using art::ScopedJniEnvLocalRefState;
using art::ThrowLocation;

#define XPOSED_JAR "system/framework/XposedBridge.jar"
#define XPOSED_CLASS "com/taobao/android/dexposed/XposedBridge"
#define XPOSED_CLASS_DOTS "com.taobao.android.dexposed.XposedBridge"

#define SHARED_LOCKS_REQUIRED(...) THREAD_ANNOTATION_ATTRIBUTE__(shared_locks_required(__VA_ARGS__))

namespace art {
    struct XposedHookInfo {
        jobject reflectedMethod;
        jobject additionalInfo;
        jobject original_method;
    };

    static bool xposedIsHooked(ArtMethod* method);

    static jboolean com_taobao_android_dexposed_XposedBridge_initNative(JNIEnv* env, jclass clazz);
    static void com_taobao_android_dexposed_XposedBridge_hookMethodNative(JNIEnv* env, jclass clazz, jobject javaMethod, jobject declaredClassIndirect, jint slot, jobject additionalInfoIndirect);

    static int register_com_taobao_android_dexposed_XposedBridge(JNIEnv* env);

} // namespace android

#endif  // XPOSED_H