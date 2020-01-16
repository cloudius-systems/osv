#include <jni.h>
#include <jvmti.h>
#include <sstream>
#include <iostream>
#include <thread>
#include <initializer_list>
#include <osv/tracecontrol.hh>
#include "../java-base/jvm/jni_helpers.hh"

static jvmtiEnv * jvmti;
static trace::generator_id id;
static std::thread::id gen_thread_id;

template<typename T>
struct JvmtiMem {
    JvmtiMem(jvmtiEnv * j) :
            value(nullptr), jvmti(j) {
    }
    ~JvmtiMem() {
        if (value != nullptr) {
            jvmti->Deallocate(reinterpret_cast<unsigned char *>(value));
        }
    }

    T * value;
    jvmtiEnv * jvmti;
};


static bool check(jvmtiError err, const char * what) {
    if (err == JVMTI_ERROR_NONE) {
        return true;
    }
    std::cerr << what << " - JVMTI error : " << err;
    if (jvmti != nullptr) {
        JvmtiMem<char> name(jvmti);
        if (jvmti->GetErrorName(err, &name.value) == JVMTI_ERROR_NONE) {
            std::cerr << "(" << name.value << ")";
        }
    }
    std::cerr << std::endl;
    return false;
}

#define CHECK(call)  check(call, #call)

static void generate_java_symbols(const trace::add_symbol_func & add_symbol) {
    assert(gen_thread_id == std::thread::id());
    gen_thread_id = std::this_thread::get_id();

    try {
        attached_env env;

        if (jvmti != nullptr) {
            CHECK(jvmti->SetEnvironmentLocalStorage(&add_symbol));
            for (auto e : { JVMTI_EVENT_COMPILED_METHOD_LOAD, JVMTI_EVENT_DYNAMIC_CODE_GENERATED }) {
                CHECK(jvmti->SetEventNotificationMode(JVMTI_ENABLE, e, nullptr));
                CHECK(jvmti->GenerateEvents(e));
                CHECK(jvmti->SetEventNotificationMode(JVMTI_DISABLE, e, nullptr));
            }
            CHECK(jvmti->SetEnvironmentLocalStorage(nullptr));
        }
    } catch (...) {
    }

    gen_thread_id = std::thread::id();
}

static void register_symbol(jvmtiEnv *jvmti_env, jint code_size,
        const void* code_addr, const char * name, jmethodID method = nullptr,
        jint map_length = 0, const jvmtiAddrLocationMap* map = nullptr)
{
    // Only do this is generator thread.
    if (gen_thread_id != std::this_thread::get_id()) {
        return;
    }

    const trace::add_symbol_func * padd_symbol = nullptr;

    if (!CHECK(jvmti_env->GetEnvironmentLocalStorage((void**) &padd_symbol)) || padd_symbol == nullptr) {
        return;
    }

    trace::symbol symb;

    symb.addr = code_addr;
    symb.size = code_size;

    JvmtiMem<char> file(jvmti_env);

    if (name != nullptr) {
        symb.name = name;
    }


    if (method != nullptr) {
        jclass klass;

        if (CHECK(jvmti_env->GetMethodDeclaringClass(method, &klass))) {
            JvmtiMem<char> name(jvmti_env);
            if (CHECK(jvmti_env->GetClassSignature(klass, &name.value, nullptr))) {
                symb.name.append(name.value + 1);
                symb.name.resize(symb.name.size() - 1);
            }
            // TODO: line numbers. Are they worth the overhead?
            jvmti_env->GetSourceFileName(klass, &file.value); // don't care if it fails.
            symb.filename = file.value;
        }
        {
            JvmtiMem<char> name(jvmti_env);
            JvmtiMem<char> desc(jvmti_env);
            if (CHECK(jvmti_env->GetMethodName(method, &name.value, &desc.value, nullptr))) {
                symb.name.append(1, '.');
                symb.name.append(name.value);
                symb.name.append(desc.value);
            }
        }
    }

    (*padd_symbol)(symb);
}

static void JNICALL compiled_method_load(jvmtiEnv *jvmti_env, jmethodID method,
        jint code_size, const void* code_addr, jint map_length,
        const jvmtiAddrLocationMap* map, const void* compile_info) {
    register_symbol(jvmti_env, code_size, code_addr, nullptr, method, map_length, map);
}

static void JNICALL
dynamic_code_generated(jvmtiEnv *jvmti_env, const char* name,
        const void* address, jint length)
{
    register_symbol(jvmti_env, length, address, name);
}

extern "C" JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    if (jvmti != nullptr) {
        return JNI_OK;
    }

    if (vm->GetEnv((void **) &jvmti, JVMTI_VERSION_1) != JNI_OK) {
        return JNI_ERR;
    }

    jvmtiCapabilities caps = { 0, };

    caps.can_generate_compiled_method_load_events = 1;

    if (!CHECK(jvmti->AddCapabilities(&caps))) {
         std::cout << "could not acquire capabilities" << std::endl;
         return JNI_ERR;
     }

    jvmtiEventCallbacks callbacks = { 0, };
    callbacks.CompiledMethodLoad = compiled_method_load;
    callbacks.DynamicCodeGenerated = dynamic_code_generated;

    if (!CHECK(jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks)))) {
        std::cout << "could not set callbacks" << std::endl;
        return JNI_ERR;
    }

    id = trace::add_symbol_callback(generate_java_symbols);

    return JNI_OK;
}

extern "C" JNIEXPORT void JNICALL
Agent_OnUnload(JavaVM *vm) {
    if (jvmti == nullptr) {
        return;
    }
    trace::remove_symbol_callback(id);
    jvmti->DisposeEnvironment();
    jvmti = nullptr;
}
