/**
 * when apiLevel < 22
 * set function :
 *      art_method->SetNativeMethod(reinterpret_cast<uint8_t *>(hookInfo));
 *
 * field:
 *      ArtMethod::entry_point_from_jni_
 *
 * void ArtMethod::SetNativeMethod(const void* native_method) {
 *     SetFieldPtr<false, true, kVerifyFlags>(
 *         OFFSET_OF_OBJECT_MEMBER(ArtMethod, entry_point_from_jni_), native_method);
 * }
 *
 * OFFSET_OF_OBJECT_MEMBER(ArtMethod, entry_point_from_jni_)
 *      => 32
 *
 *
 * when apiLevel >= 22
 * set function :
 *      art_method->SetEntryPointFromJni(reinterpret_cast<void *>(hookInfo));
 *
 * field:
 *     ArtMethod::PtrSizedFields::entry_point_from_jni_
 *
 * void SetEntryPointFromJni(const void* entrypoint) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
 *      CheckObjectSizeEqualsMirrorSize();
 *      SetEntryPointFromJniPtrSize<kVerifyFlags>(entrypoint, sizeof(void*));
 * }
 *
 * PtrSizedFieldsOffset(pointer_size) + OFFSETOF_MEMBER(PtrSizedFields, entry_point_from_jni_) / sizeof(void*) * pointer_size
 *      => 40
 */

#ifndef DEXPOSED_NEWARTMETHOD_H
#define DEXPOSED_NEWARTMETHOD_H

#include <mirror/art_method.h>
#include <mirror/object.h>
#include <verify_object.h>
#include <dex_file.h>
#include <mirror/dex_cache.h>

using art::mirror::ArtMethod;
using art::mirror::Object;
using art::kDefaultVerifyFlags;
using art::DexFile;
using art::mirror::DexCache;
using art::MemberOffset;

class ArtMethodProxy {

public:
    enum class Member{
        entry_point_from_quick_comiled_code_,
        entry_point_from_jni_,
        dex_method_index_,
        declaring_class_,
        dex_cache_resolved_methods_,
        access_flags_,
    };

    explicit ArtMethodProxy(ArtMethod* art_method, int running_sdk_version)
            : art_method(art_method),running_sdk_version(running_sdk_version){}

    const char* GetShorty()
    {
        uint32_t out_length;
        return GetShorty(&out_length);
    }

    const char* GetShorty(uint32_t* out_length)
    {
        ArtMethodProxy* proxy = GetInterfaceMethodIfProxy();
        const DexFile* dex_file = proxy->GetDexFile();
        return dex_file->GetMethodShorty(dex_file->GetMethodId(GetDexMethodIndex()), out_length);
    }

    void* GetFieldPtr(Member member_name)
    {
        if(4 == sizeof(void *))
            return reinterpret_cast<void *>(art_method->GetField32<kDefaultVerifyFlags,false>(this->GetMemberOffset(member_name)));
        else
            return reinterpret_cast<void *>(art_method->GetField64<kDefaultVerifyFlags,false>(this->GetMemberOffset(member_name)));
    }

    void SetFieldPtr(Member member_name,void* value)
    {
        art_method->SetFieldPtr<false, false, art::kDefaultVerifyFlags>(this->GetMemberOffset(member_name),value);
    }

    template<class T>
    T* GetFieldObject(Member member_name)
    {
        return art_method->GetFieldObject<T>(this->GetMemberOffset(member_name));
    }

    ArtMethodProxy* GetInterfaceMethodIfProxy() {

        if (LIKELY(!IsProxyMethod())) {
            return this;
        }
        Class* klass = GetDeclaringClass();
        ArtMethod* interface_method = GetDexCacheResolvedMethods()->Get(GetDexMethodIndex());
        return new ArtMethodProxy(interface_method,running_sdk_version);
    }

    uint32_t GetDexMethodIndex() {
        reinterpret_cast<uint32_t>(GetFieldPtr(Member::dex_method_index_));
    }

    Class* GetDeclaringClass() {
        return GetFieldObject<Class>(Member::declaring_class_);
    }

    bool IsProxyMethod() {
        return GetDeclaringClass()->IsProxyClass();
    }

    ObjectArray<ArtMethod>* GetDexCacheResolvedMethods() {
        return GetFieldObject<ObjectArray<ArtMethod>>(Member::dex_cache_resolved_methods_);
    }

    const DexFile* GetDexFile() {
        return GetDexCache()->GetDexFile();
    }

    DexCache* GetDexCache() {
        return GetInterfaceMethodIfProxy()->GetDeclaringClass()->GetDexCache();
    }

    static void ShowOriginalOffset()
    {
        LOG(INFO) << "OFFSET_OF_OBJECT_MEMBER >>> dex_method_index_ " << OFFSET_OF_OBJECT_MEMBER(ArtMethod, dex_method_index_);
        LOG(INFO) << "OFFSET_OF_OBJECT_MEMBER >>> declaring_class_ " << OFFSET_OF_OBJECT_MEMBER(ArtMethod, declaring_class_);
        LOG(INFO) << "OFFSET_OF_OBJECT_MEMBER >>> dex_cache_resolved_methods_ " << OFFSET_OF_OBJECT_MEMBER(ArtMethod, dex_cache_resolved_methods_);
        LOG(INFO) << "OFFSET_OF_OBJECT_MEMBER >>> access_flags_ " << OFFSET_OF_OBJECT_MEMBER(ArtMethod, access_flags_);
    }

private:
    MemberOffset GetMemberOffset(Member member)
    {
        if(running_sdk_version >= 22)
            return art::MemberOffset(this->member_offset_map[member][1]);
        else
            return art::MemberOffset(this->member_offset_map[member][0]);
    }
public:


private:

    ArtMethod* art_method;
    int running_sdk_version;

    std::map<Member, std::vector<int>> member_offset_map {
        { Member::entry_point_from_quick_comiled_code_, {40, 44} },
        { Member::entry_point_from_jni_, {32, 40} },
        { Member::dex_method_index_, {64, 28} },
        { Member::declaring_class_, {8, 8} },
        { Member::dex_cache_resolved_methods_, {12, 12} },
        { Member::access_flags_, {56, 20} },

    };
};


#endif //DEXPOSED_NEWARTMETHOD_H
