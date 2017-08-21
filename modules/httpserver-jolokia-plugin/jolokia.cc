/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "routes.hh"
#include "jolokia.hh"
#include "autogen/jolokia.json.hh"
#include "json/formatter.hh"
#include "mime_types.hh"

#include <string>
#include <mutex>
#include <memory>
#include <regex>
#include <java/jvm/jni_helpers.hh>
#include "exception.hh"

using namespace httpserver::json;
using namespace httpserver::json::jolokia_json;

static void verify_jvm() {
    if (!jvm_getter::is_jvm_running()) {
        throw httpserver::not_found_exception("JVM not running");
    }
}
extern "C" void httpserver_plugin_register_routes(httpserver::routes* routes) {
    httpserver::api::jolokia::init(*routes);
}
/**
 * Initialize the routes object with specific routes mapping
 * @param routes - the routes object to fill
 */
void httpserver::api::jolokia::init(routes & routes)
{
    // Holder for all the java types and ID:s we need to dispatch calls.
    // Since this is shared between three handler instances, we keep it in a separate type.

    struct classes {
        void init() {

            std::call_once(flag,
                    [this]() {

                        attached_env aenv;

#define CDSP "io/osv/jolokia/Dispatcher"
#define CREQ "io/osv/jolokia/RequestAndResponse"
#define CURL "java/net/URL"
#define CUCL "java/net/URLClassLoader"
#define SURL "L" CURL ";"
#define SSTR "Ljava/lang/String;"

                    jexception_check chk;

                    jclazz url_clz(CURL);
                    jclazz urlcl_clz(CUCL);
                    methodID url_init(url_clz, "<init>", "(" SSTR ")V");
                    methodID urlcl_init(urlcl_clz, "<init>", "([" SURL ")V");

                    jobject url = aenv.env()->NewObject(url_clz, url_init, to_jstring("file:///usr/mgmt/jolokia-agent.jar"));
                    jobjectArray urls = aenv.env()->NewObjectArray(1, url_clz, url);
                    jobject loader = aenv.env()->NewObject(urlcl_clz, urlcl_init, urls);

                    jclazz disp_clz(CDSP, loader);
                    methodID disp_init(disp_clz, "<init>", "()V");

                    dispatcher = aenv.env()->NewObject(disp_clz, disp_init);
                    dispDisp = methodID(disp_clz, "dispatch", "(L" CREQ ";)I");
                    reqresp = jclazz(CREQ, loader);
                    rrInit = methodID(reqresp, "<init>", "(I" SSTR SSTR SSTR "[B)V");
                    addParameter = methodID(reqresp, "addParameter", "(" SSTR SSTR ")V");
                    rrResp = fieldID(reqresp, "response", SSTR);
                    rrMime = fieldID(reqresp, "mimeType", SSTR);
                });
        }

        std::once_flag flag;

        jglobal<jobject>
                dispatcher;
        jglobal<jclass>
                reqresp;
        jmethodID rrInit;
        jmethodID dispDisp;
        jmethodID addParameter;
        jfieldID rrResp;
        jfieldID rrMime;
    };

    class jolokia_handler : public handler_base {
    public:
        jolokia_handler(const std::shared_ptr<classes> & clz) : _clz(clz)
        {}
        void handle(const std::string &, parameters * params,
                const http::server::request& req, http::server::reply& rep)
                        override
                        {
            verify_jvm();
            attached_env aenv;

            _clz->init();

            jexception_check chk;

            jstring enc = nullptr;
            jbyteArray body = nullptr;

            jint mode = 0;

            if (req.method == "POST") {
                mode = 1;

                auto ctype = req.get_header("Content-Type");

                // This looks like a perfect opportunity for a regex, no?
                // Yes, if it had not been the case that regex in gcc 4.8 series
                // is virtually unusable (throws exceptions on valid regexes, off-by-one
                // errors, etc etc).
                // See
                //  https://gcc.gnu.org/bugzilla/show_bug.cgi?id=61464
                //  https://gcc.gnu.org/ml/gcc-bugs/2014-01/msg01083.html
                // Or just try writing it yourself and see. Gah.
                //
                // Should be fixed in gcc 4.9, but we don't explicitly use that,
                // so...

                auto off = ctype.find("charset=");
                auto end = ctype.find_first_of(" \t\n\r", off + 1);

                if (off != std::string::npos) {
                   auto cs = ctype.substr(off + 8, end - (off + 8));
                   enc = to_jstring(cs);
                }

                auto len = req.content_length;

                body = aenv.env()->NewByteArray(jint(len));
                aenv.env()->SetByteArrayRegion(body, 0, len, reinterpret_cast<const jbyte *>(req.content.c_str()));
            }

            std::string path = req.param.count("query") > 0 ? req.param.at("query") : "";
            std::string uri = "http://localhost" + req.uri;

            jobject jreq = aenv.env()->NewObject(_clz->reqresp, _clz->rrInit,
                    mode, to_jstring(uri), to_jstring(path), enc, body);

            for (auto & p : req.query_parameters) {
                jexception_check chk;
                aenv.env()->CallVoidMethod(jreq, _clz->addParameter, to_jstring(p.name), to_jstring(p.value));
            }

            jint res;

            {
                jexception_check chk;
                res = aenv.env()->CallIntMethod(_clz->dispatcher, _clz->dispDisp, jreq);
            }

            jstring jresp = (jstring)aenv.env()->GetObjectField(jreq, _clz->rrResp);
            jstring jmime = (jstring)aenv.env()->GetObjectField(jreq, _clz->rrMime);

            rep.content = from_jstring(jresp);
            rep.status = http::server::reply::status_type(res);

            if (jmime != nullptr) {
                set_headers_explicit(rep, from_jstring(jmime));
            } else {
                set_headers(rep, "json");
            }
        }
        std::shared_ptr<classes> _clz;
    };

    jolokia_json_init_path("Jolokia API");

    std::shared_ptr<classes> clz = std::make_shared<classes>();

    jolokia_json::getJolokia.set_handler(new jolokia_handler(clz));
    jolokia_json::getJolokiaAlt.set_handler(new jolokia_handler(clz));
    jolokia_json::postJolokia.set_handler(new jolokia_handler(clz));
}
